package com.zhuotong.myihk;

import org.json.JSONObject;

import java.lang.reflect.Method;
import java.net.NetworkInterface;
import java.util.Collections;
import java.util.List;

public class Utils {

    public static String getSystemProperites(String key) {
        try {
            Method getMethod = Class.forName("android.os.SystemProperties").getMethod("get", String.class);
            return (String) getMethod.invoke(null, key);
        } catch (Exception e) {
            e.printStackTrace();
        }
        return null;
    }

    public static JSONObject getInterfacesHDAddresses() {
        JSONObject addressInfo = new JSONObject();
        try {
            List<NetworkInterface> allInterfaces = Collections.list(NetworkInterface.getNetworkInterfaces());
            for (NetworkInterface netInterface : allInterfaces) {
                try {
                    String ifName = netInterface.getName();
                    byte[] hardwareAddressBytes = netInterface.getHardwareAddress();
                    if (hardwareAddressBytes != null) {
                        StringBuilder stringBuilder = new StringBuilder();
                        for (byte b : hardwareAddressBytes) {
                            stringBuilder.append(String.format("%02X:", b));    // 02x for lowercase
                        }
                        if (stringBuilder.length() > 0) {
                            stringBuilder.deleteCharAt(stringBuilder.length() - 1);
                        }

                        String address = stringBuilder.toString();
                        addressInfo.put(ifName, address);
                    }
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }
        } catch (Exception e) {
            e.printStackTrace();
        }
        return addressInfo;
    }


}
