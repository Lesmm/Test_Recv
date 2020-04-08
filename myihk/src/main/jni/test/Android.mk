LOCAL_PATH := $(call my-dir)  


### 以下这段用于预构建我们将要连接的已存在的静态库或动态库 ###




include $(CLEAR_VARS)

# 我们将连接已编译好的 libjsoncpp 模块
LOCAL_MODULE    := libjsoncpp
# 填写源文件名的时候，要把静态库或动态库的文件名填写完整。
# $(TARGET_ARCH_ABI)/ 表示将不同架构下的库文件存放到相应架构目录下
LOCAL_SRC_FILES := $(LOCAL_PATH)/../../libs/$(TARGET_ARCH_ABI)/libjsoncpp.a
# 用于预构建静态库（后面可被连接）
include $(PREBUILT_STATIC_LIBRARY)





include $(CLEAR_VARS)

$(warning "abi: $(TARGET_ARCH_ABI)")

#ifeq "$(TARGET_ARCH_ABI)" "arm64-v8a"

#LOCAL_CXXFLAGS +=  -g -O0
#LOCAL_ARM_MODE := arm
LOCAL_MODULE    		:= services
LOCAL_STATIC_LIBRARIES	:= imodel #dump_with_ret dump replace
LOCAL_C_INCLUDES 		:= $(LOCAL_PATH)/../include
LOCAL_SRC_FILES 		:= main.cpp ifaddress.cpp
LOCAL_LDLIBS 			+= -L$(SYSROOT)/usr/lib -llog

# 连接我们前面声明好的静态库
LOCAL_STATIC_LIBRARIES += libjsoncpp

include $(BUILD_SHARED_LIBRARY)

#endif