//
// Created by EDZ on 2019/12/18.
//

#include "mhk.h"

#include <jni.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/system_properties.h>

#include <sys/socket.h>

#include "iModel.h"
#include "ifaddress.h"

// --------------------- fopen ---------------------
typedef FILE *(*old_fopen)(const char *__path, const char *__mode);

static inline bool needs_mode(int flags) {
    return ((flags & O_CREAT) == O_CREAT) || ((flags & O_TMPFILE) == O_TMPFILE);
}

FILE *my_fopen(const char *pathname, const char *mode) {
    LE("hk_fopen %s , %s", pathname ? pathname : "is null", mode);

    FILE *fd = NULL;
    auto ori_open = (old_fopen) (getOriFunByHkFun((void *) (my_fopen)));
    if (!ori_open) {
        ori_open = (old_fopen) fopen;
    }

    fd = ori_open(pathname, mode);
    LE("ori: fopen %s , fd=%p", pathname ? pathname : "is null", fd);
    return fd;
}

void test__fopen() {
    const RetInfo info = dump_replace((void *) fopen, (void *) (my_fopen), NULL,
                                      NULL, "fopen");
    if (info.status != success) {
        LE("test__fopen error=%d", info.status);
    }
}
// --------------------- fopen ---------------------


// --------------------- popen ---------------------
typedef FILE *(*old_popen)(const char *__command, const char *__mode);

FILE *my_popen(const char *__command, const char *mode) {
    LE("hk_popen %s , %s", __command ? __command : "is null", mode);

    FILE *fd = NULL;
    auto ori_open = (old_popen) (getOriFunByHkFun((void *) (my_popen)));
    if (!ori_open) {
        ori_open = (old_popen) popen;
    }

    fd = ori_open(__command, mode);
    LE("ori: popen %s , fd=%p", __command ? __command : "is null", fd);
    return fd;
}

void test__popen() {
    const RetInfo info = dump_replace((void *) popen, (void *) (my_popen), NULL,
                                      NULL, "popen");
    if (info.status != success) {
        LE("test__popen error=%d", info.status);
    }
}
// --------------------- popen ---------------------



// --------------------- __system_property_get ---------------------

int my__system_property_get(const char *name, char *value) {
    LE("hk: __system_property_gcat /et(%s, %p)", name ? name : "is null", value);
    int (*ori__system_property_get)(const char *name, char *value) = (int (*)(const char *, char *)) (getOriFunByHkFun((void *) (my__system_property_get)));
    if (!ori__system_property_get) {
        ori__system_property_get = __system_property_get;
    }
    int ret = ori__system_property_get(name, value);
    LE("ori: __system_property_get(%s, %s)=%d", name ? name : "is null", ret > 0 ? value : "is null", ret);
    return ret;
}

void test__system_property_get() {
    const RetInfo info = dump_replace((void *) __system_property_get, (void *) (my__system_property_get), NULL,
                                      NULL, "__system_property_get");
    if (info.status != success) {
        LE("test__system_property_get error=%d", info.status);
    }
}
// --------------------- __system_property_get ---------------------


// --------------------- recv ---------------------  // 指令修复不完全，回调原始函数会崩溃
typedef ssize_t (*type_recv)(int __fd, void *__buf, size_t __n, int __flags);

type_recv *mOriginalRecv;

ssize_t my__recv(int __fd, void *__buf, size_t __n, int __flags) {
    LE("my__recv(%d, %p, %ld, %d)", __fd, __buf, __n, __flags);

//    type_recv or_recv = (type_recv) (getOriFunByHkFun((void *) (my__recv)));
//    if (!or_recv) {
//        or_recv = (type_recv) recv;
//    }
//    ssize_t result = (*or_recv)(__fd, __buf, __n, __flags);       // 指令修复不完全，回调原始函数会崩溃

    ssize_t result = (*mOriginalRecv)(__fd, __buf, __n, __flags);   // 指令修复不完全，回调原始函数会崩溃
    return result;
}

void test__recv() {
    const RetInfo info = dump_replace((void *) recv, (void *) (my__recv), NULL, NULL, "recv");
    if (info.status != success) {
        LE("hk__recv error = %d", info.status);
    } else {
        mOriginalRecv = (type_recv *) (getPoriFun(info.info));
    }
}
// --------------------- recv ---------------------



JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    JNIEnv *env = NULL;
    if (vm->GetEnv((void **) &env, JNI_VERSION_1_4) != JNI_OK) {
        return -1;
    }

    test__system_property_get();

    test__fopen();

    test__popen();

    test__recv();

    return JNI_VERSION_1_4;
}


extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_zhuotong_myihk_MainActivity_invokeNativeMethod(JNIEnv *env, jobject jobject) {

    // 1. 测试 __system_property_get
    char fingerBuffer[256];
    int length = __system_property_get("ro.build.fingerprint", fingerBuffer);
    LOGI("ro.build.fingerprint=%s", length > 0 ? fingerBuffer : "is null");

    // 2. 测试 fopen
    FILE *filePtr = fopen("/sys/class/android_usb/android0/iSerial", "r");
    if (filePtr != NULL) {
        fseek(filePtr, 0, SEEK_END);
        int size = (int) ftell(filePtr);
        char *fileBuffer = new char[size + 1];
        rewind(filePtr);
        fread(fileBuffer, 1, (size_t) size, filePtr);
        fileBuffer[size] = 0;
        fclose(filePtr);
        LOGI("iSerial fopen=%s", fileBuffer);
        delete (fileBuffer);
    }

    // 3. 测试 popen
    char serialBuffer[128] = {0};
    memset(serialBuffer, 0, sizeof(serialBuffer));
    FILE *fp = popen("cat /sys/class/android_usb/android0/iSerial", "r");
    fread(serialBuffer, 1, 127, fp);
    pclose(fp);
    LOGI("iSerial popen=%s", serialBuffer);

    // 3. 测试 recv
    jobjectArray macs = Linux_getifaddrs(env);
    return macs;
}
