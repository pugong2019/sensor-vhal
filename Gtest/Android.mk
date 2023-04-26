LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_LDLIBS += -landroid -llog
LOCAL_CFLAGS += -std=c++11
LOCAL_CFLAGS += -Wno-unused-parameter -Wno-macro-redefined -fexceptions

LOCAL_MODULE_TAGS:= optional

LOCAL_SHARED_LIBRARIES += liblog libcutils libutils
LOCAL_MULTILIB := 64
LOCAL_VENDOR_MODULE := true
LOCAL_STATIC_LIBRARIES += libgtest_main libgtest libgmock

LOCAL_CPPFLAGS := $(LOG_FLAGS) $(WARNING_LEVEL) $(DEBUG_FLAGS) $(VERSION_FLAGS)

LOCAL_C_INCLUDES += \
    $(LOCAL_PATH) \
    $(LOCAL_PATH)/../ \
    $(LOCAL_PATH)/../sock_utils/include \
    $(LOCAL_PATH)/../../../external/googletest/googletest/include

LOCAL_SRC_FILES := \
    main.cpp \
    sensor_client.cpp \
    ../sock_utils/sock_utils.cpp \
    ../sock_utils/sock_client.cpp \
    ../sock_utils/sock_server.cpp

LOCAL_MODULE:= SensorTest

include $(BUILD_EXECUTABLE)
