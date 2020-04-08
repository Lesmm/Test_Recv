//
// Created by ubuntu on 3/30/20.
//
#include <jni.h>
#include <android/log.h>

#include <sys/socket.h>
#include <ifaddrs.h>
#include <linux/rtnetlink.h>

#include <ifaddrs.h>

#include <errno.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct nlmsghdr;

class NetlinkConnection {
public:
    NetlinkConnection();

    ~NetlinkConnection();

    bool SendRequest(int type);

    bool ReadResponses(void callback(void *, nlmsghdr *), void *context);

private:
    int fd_;
    char *data_;
    size_t size_;
};

NetlinkConnection::NetlinkConnection() {
    fd_ = -1;

    // The kernel keeps packets under 8KiB (NLMSG_GOODSIZE),
    // but that's a bit too large to go on the stack.
    size_ = 8192;
    data_ = new char[size_];
}

NetlinkConnection::~NetlinkConnection() {
    if (fd_ != -1) close(fd_);
    delete[] data_;
}

bool NetlinkConnection::SendRequest(int type) {
    // Rather than force all callers to check for the unlikely event of being
    // unable to allocate 8KiB, check here.
    if (data_ == nullptr) return false;

    // Did we open a netlink socket yet?
    if (fd_ == -1) {
        fd_ = socket(PF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
        if (fd_ == -1) return false;
    }

    // Construct and send the message.
    struct NetlinkMessage {
        nlmsghdr hdr;
        rtgenmsg msg;
    } request;
    memset(&request, 0, sizeof(request));
    request.hdr.nlmsg_flags = NLM_F_DUMP | NLM_F_REQUEST;
    request.hdr.nlmsg_type = type;
    request.hdr.nlmsg_len = sizeof(request);
    request.msg.rtgen_family = AF_UNSPEC; // All families.
    return (TEMP_FAILURE_RETRY(send(fd_, &request, sizeof(request), 0)) == sizeof(request));
}

bool NetlinkConnection::ReadResponses(void callback(void *, nlmsghdr *), void *context) {
    // Read through all the responses, handing interesting ones to the callback.
    ssize_t bytes_read;
    while ((bytes_read = TEMP_FAILURE_RETRY(recv(fd_, data_, size_, 0))) > 0) {
        nlmsghdr *hdr = reinterpret_cast<nlmsghdr *>(data_);
        for (; NLMSG_OK(hdr, static_cast<size_t>(bytes_read)); hdr = NLMSG_NEXT(hdr, bytes_read)) {
            if (hdr->nlmsg_type == NLMSG_DONE) return true;
            if (hdr->nlmsg_type == NLMSG_ERROR) {
                nlmsgerr *err = reinterpret_cast<nlmsgerr *>(NLMSG_DATA(hdr));
                errno = (hdr->nlmsg_len >= NLMSG_LENGTH(sizeof(nlmsgerr))) ? -err->error : EIO;
                return false;
            }
            callback(context, hdr);
        }
    }

    // We only get here if recv fails before we see a NLMSG_DONE.
    return false;
}

struct ifaddrs_storage {
    // Must come first, so that `ifaddrs_storage` is-a `ifaddrs`.
    ifaddrs ifa;

    // The interface index, so we can match RTM_NEWADDR messages with
    // earlier RTM_NEWLINK messages (to copy the interface flags).
    int interface_index;

    // Storage for the pointers in `ifa`.
    sockaddr_storage addr;
    sockaddr_storage netmask;
    sockaddr_storage ifa_ifu;
    char name[IFNAMSIZ + 1];

    explicit ifaddrs_storage(ifaddrs **list) {
        memset(this, 0, sizeof(*this));

        // push_front onto `list`.
        ifa.ifa_next = *list;
        *list = reinterpret_cast<ifaddrs *>(this);
    }

    void SetAddress(int family, const void *data, size_t byteCount) {
        // The kernel currently uses the order IFA_ADDRESS, IFA_LOCAL, IFA_BROADCAST
        // in inet_fill_ifaddr, but let's not assume that will always be true...
        if (ifa.ifa_addr == nullptr) {
            // This is an IFA_ADDRESS and haven't seen an IFA_LOCAL yet, so assume this is the
            // local address. SetLocalAddress will fix things if we later see an IFA_LOCAL.
            ifa.ifa_addr = CopyAddress(family, data, byteCount, &addr);
        } else {
            // We already saw an IFA_LOCAL, which implies this is a destination address.
            ifa.ifa_dstaddr = CopyAddress(family, data, byteCount, &ifa_ifu);
        }
    }

    void SetBroadcastAddress(int family, const void *data, size_t byteCount) {
        // ifa_broadaddr and ifa_dstaddr overlap in a union. Unfortunately, it's possible
        // to have an interface with both. Keeping the last thing the kernel gives us seems
        // to be glibc 2.19's behavior too, so our choice is being source compatible with
        // badly-written code that assumes ifa_broadaddr and ifa_dstaddr are interchangeable
        // or supporting interfaces with both addresses configured. My assumption is that
        // bad code is more common than weird network interfaces...
        ifa.ifa_broadaddr = CopyAddress(family, data, byteCount, &ifa_ifu);
    }

    void SetLocalAddress(int family, const void *data, size_t byteCount) {
        // The kernel source says "for point-to-point IFA_ADDRESS is DESTINATION address,
        // local address is supplied in IFA_LOCAL attribute".
        //   -- http://lxr.free-electrons.com/source/include/uapi/linux/if_addr.h#L17

        // So copy any existing IFA_ADDRESS into ifa_dstaddr...
        if (ifa.ifa_addr != nullptr) {
            ifa.ifa_dstaddr = reinterpret_cast<sockaddr *>(memcpy(&ifa_ifu, &addr, sizeof(addr)));
        }
        // ...and then put this IFA_LOCAL into ifa_addr.
        ifa.ifa_addr = CopyAddress(family, data, byteCount, &addr);
    }

    // Netlink gives us the prefix length as a bit count. We need to turn
    // that into a BSD-compatible netmask represented by a sockaddr*.
    void SetNetmask(int family, size_t prefix_length) {
        // ...and work out the netmask from the prefix length.
        netmask.ss_family = family;
        uint8_t *dst = SockaddrBytes(family, &netmask);
        memset(dst, 0xff, prefix_length / 8);
        if ((prefix_length % 8) != 0) {
            dst[prefix_length / 8] = (0xff << (8 - (prefix_length % 8)));
        }
        ifa.ifa_netmask = reinterpret_cast<sockaddr *>(&netmask);
    }

    void SetPacketAttributes(int ifindex, unsigned short hatype, unsigned char halen) {
        sockaddr_ll *sll = reinterpret_cast<sockaddr_ll *>(&addr);
        sll->sll_ifindex = ifindex;
        sll->sll_hatype = hatype;
        sll->sll_halen = halen;
    }

private:
    sockaddr *CopyAddress(int family, const void *data, size_t byteCount, sockaddr_storage *ss) {
        // Netlink gives us the address family in the header, and the
        // sockaddr_in or sockaddr_in6 bytes as the payload. We need to
        // stitch the two bits together into the sockaddr that's part of
        // our portable interface.
        ss->ss_family = family;
        memcpy(SockaddrBytes(family, ss), data, byteCount);

        // For IPv6 we might also have to set the scope id.
        if (family == AF_INET6 && (IN6_IS_ADDR_LINKLOCAL(data) || IN6_IS_ADDR_MC_LINKLOCAL(data))) {
            reinterpret_cast<sockaddr_in6 *>(ss)->sin6_scope_id = interface_index;
        }

        return reinterpret_cast<sockaddr *>(ss);
    }

    // Returns a pointer to the first byte in the address data (which is
    // stored in network byte order).
    uint8_t *SockaddrBytes(int family, sockaddr_storage *ss) {
        if (family == AF_INET) {
            sockaddr_in *ss4 = reinterpret_cast<sockaddr_in *>(ss);
            return reinterpret_cast<uint8_t *>(&ss4->sin_addr);
        } else if (family == AF_INET6) {
            sockaddr_in6 *ss6 = reinterpret_cast<sockaddr_in6 *>(ss);
            return reinterpret_cast<uint8_t *>(&ss6->sin6_addr);
        } else if (family == AF_PACKET) {
            sockaddr_ll *sll = reinterpret_cast<sockaddr_ll *>(ss);
            return reinterpret_cast<uint8_t *>(&sll->sll_addr);
        }
        return nullptr;
    }
};

static void __getifaddrs_callback(void *context, nlmsghdr *hdr) {
    ifaddrs **out = reinterpret_cast<ifaddrs **>(context);

    if (hdr->nlmsg_type == RTM_NEWLINK) {
        ifinfomsg *ifi = reinterpret_cast<ifinfomsg *>(NLMSG_DATA(hdr));

        // Create a new ifaddr entry, and set the interface index and flags.
        ifaddrs_storage *new_addr = new ifaddrs_storage(out);
        new_addr->interface_index = ifi->ifi_index;
        new_addr->ifa.ifa_flags = ifi->ifi_flags;

        // Go through the various bits of information and find the name.
        rtattr *rta = IFLA_RTA(ifi);
        size_t rta_len = IFLA_PAYLOAD(hdr);
        while (RTA_OK(rta, rta_len)) {
            if (rta->rta_type == IFLA_ADDRESS) {
                if (RTA_PAYLOAD(rta) < sizeof(new_addr->addr)) {
                    new_addr->SetAddress(AF_PACKET, RTA_DATA(rta), RTA_PAYLOAD(rta));
                    new_addr->SetPacketAttributes(ifi->ifi_index, ifi->ifi_type, RTA_PAYLOAD(rta));
                }
            } else if (rta->rta_type == IFLA_BROADCAST) {
                if (RTA_PAYLOAD(rta) < sizeof(new_addr->ifa_ifu)) {
                    new_addr->SetBroadcastAddress(AF_PACKET, RTA_DATA(rta), RTA_PAYLOAD(rta));
                    new_addr->SetPacketAttributes(ifi->ifi_index, ifi->ifi_type, RTA_PAYLOAD(rta));
                }
            } else if (rta->rta_type == IFLA_IFNAME) {
                if (RTA_PAYLOAD(rta) < sizeof(new_addr->name)) {
                    memcpy(new_addr->name, RTA_DATA(rta), RTA_PAYLOAD(rta));
                    new_addr->ifa.ifa_name = new_addr->name;
                }
            }
            rta = RTA_NEXT(rta, rta_len);
        }
    } else if (hdr->nlmsg_type == RTM_NEWADDR) {
        ifaddrmsg *msg = reinterpret_cast<ifaddrmsg *>(NLMSG_DATA(hdr));

        // We should already know about this from an RTM_NEWLINK message.
        const ifaddrs_storage *addr = reinterpret_cast<const ifaddrs_storage *>(*out);
        while (addr != nullptr && addr->interface_index != static_cast<int>(msg->ifa_index)) {
            addr = reinterpret_cast<const ifaddrs_storage *>(addr->ifa.ifa_next);
        }
        // If this is an unknown interface, ignore whatever we're being told about it.
        if (addr == nullptr) return;

        // Create a new ifaddr entry and copy what we already know.
        ifaddrs_storage *new_addr = new ifaddrs_storage(out);
        // We can just copy the name rather than look for IFA_LABEL.
        strcpy(new_addr->name, addr->name);
        new_addr->ifa.ifa_name = new_addr->name;
        new_addr->ifa.ifa_flags = addr->ifa.ifa_flags;
        new_addr->interface_index = addr->interface_index;

        // Go through the various bits of information and find the address
        // and any broadcast/destination address.
        rtattr *rta = IFA_RTA(msg);
        size_t rta_len = IFA_PAYLOAD(hdr);
        while (RTA_OK(rta, rta_len)) {
            if (rta->rta_type == IFA_ADDRESS) {
                if (msg->ifa_family == AF_INET || msg->ifa_family == AF_INET6) {
                    new_addr->SetAddress(msg->ifa_family, RTA_DATA(rta), RTA_PAYLOAD(rta));
                    new_addr->SetNetmask(msg->ifa_family, msg->ifa_prefixlen);
                }
            } else if (rta->rta_type == IFA_BROADCAST) {
                if (msg->ifa_family == AF_INET) {
                    new_addr->SetBroadcastAddress(msg->ifa_family, RTA_DATA(rta), RTA_PAYLOAD(rta));
                }
            } else if (rta->rta_type == IFA_LOCAL) {
                if (msg->ifa_family == AF_INET || msg->ifa_family == AF_INET6) {
                    new_addr->SetLocalAddress(msg->ifa_family, RTA_DATA(rta), RTA_PAYLOAD(rta));
                }
            }
            rta = RTA_NEXT(rta, rta_len);
        }
    }
}

void __freeifaddrs(ifaddrs *list) {
    while (list != nullptr) {
        ifaddrs *current = list;
        list = list->ifa_next;
        free(current);
    }
}

int __getifaddrs(ifaddrs **out) {
    // We construct the result directly into `out`, so terminate the list.
    *out = nullptr;

    // Open the netlink socket and ask for all the links and addresses.
    NetlinkConnection nc;
    bool okay = nc.SendRequest(RTM_GETLINK) && nc.ReadResponses(__getifaddrs_callback, out) &&
                nc.SendRequest(RTM_GETADDR) && nc.ReadResponses(__getifaddrs_callback, out);
    if (!okay) {
        __freeifaddrs(*out);
        // Ensure that callers crash if they forget to check for success.
        *out = nullptr;
        return -1;
    }

    return 0;
}


jobjectArray Linux_getifaddrs(JNIEnv *env) {
    ifaddrs *ifaddr;
    int rc = TEMP_FAILURE_RETRY(__getifaddrs(&ifaddr));
//    int rc = TEMP_FAILURE_RETRY(getifaddrs(&ifaddr));
    if (rc == -1) {
        return NULL;
    }

    // Prepare output array.
    jclass String = env->FindClass("java/lang/String");
    jobjectArray result = env->NewObjectArray(64, String, NULL);
    if (result == NULL) {
        return NULL;
    }

    // Traverse the list and populate the output array.
    int index = 0;
    for (ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        char *ifa_name = ifa->ifa_name;
        sockaddr_storage *interfaceAddr = reinterpret_cast<sockaddr_storage *>(ifa->ifa_addr);
        if (interfaceAddr == NULL) {
            continue;
        }
        switch (interfaceAddr->ss_family) {
            case AF_INET:
            case AF_INET6:
                break;
            case AF_PACKET:
                // Raw Interface.
                sockaddr_ll *sll = reinterpret_cast<sockaddr_ll *>(ifa->ifa_addr);

                bool allZero = true;
                for (int i = 0; i < sll->sll_halen; ++i) {
                    if (sll->sll_addr[i] != 0) {
                        allZero = false;
                        break;
                    }
                }

                if (!allZero) {
                    jbyteArray hwaddr = env->NewByteArray(sll->sll_halen);
                    if (hwaddr != NULL) {
                        env->SetByteArrayRegion(hwaddr, 0, sll->sll_halen, reinterpret_cast<const jbyte *>(sll->sll_addr));

                        jboolean isCopy;
                        jbyte *bytes = env->GetByteArrayElements(hwaddr, &isCopy);
                        jsize length = env->GetArrayLength(hwaddr);

                        char *buffer = (char *) malloc(length + 1);
                        memcpy(buffer, bytes, length);
                        buffer[length] = '\0';

                        int one_hex_length = 3;
                        char *address_hexs = new char[length * 3 + 1];
                        for (int i = 0; i < length; i++) {
                            char *hex = new char[one_hex_length];
                            memset(hex, one_hex_length, 0);
                            sprintf(hex, "%02X:", buffer[i]);
                            memcpy(address_hexs + i * one_hex_length, hex, one_hex_length);
                            delete[] (hex);
                        }
                        address_hexs[length * one_hex_length - 1] = '\0';

                        __android_log_print(ANDROID_LOG_DEBUG, "DEBUG", "MAC OF %s: %s\n", ifa_name, address_hexs);

                        jstring name = env->NewStringUTF(ifa_name);
                        jstring mac = env->NewStringUTF(address_hexs);
                        env->SetObjectArrayElement(result, index, name);
                        ++index;
                        env->SetObjectArrayElement(result, index, mac);
                        ++index;
                    }
                }
                break;
        }
    }

    return result;
}