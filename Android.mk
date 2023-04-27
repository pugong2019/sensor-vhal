# Copyright (C) 2009 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


LOCAL_PATH := $(call my-dir)

# HAL module implemenation stored in
# hw/<SENSORS_HARDWARE_MODULE_ID>.<ro.hardware>.so

include $(CLEAR_VARS)
LOCAL_VENDOR_MODULE := true
LOCAL_CFLAGS    += -Wno-unused-parameter -Wno-macro-redefined
LOCAL_CFLAGS += -std=c++11
LOCAL_C_INCLUDES := $(LOCAL_PATH)/sock_utils/include
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE := sensors.cic_cloud
LOCAL_CFLAGS += -DLOG_TAG=\"sensors_vhal\"
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_SHARED_LIBRARIES := liblog libcutils
LOCAL_HEADER_LIBRARIES := libhardware_headers
LOCAL_MULTILIB := both
# LOCAL_C_INCLUDES += ./
LOCAL_SRC_FILES := \
		sensors_vhal.cpp \
		sock_utils/sock_client.cpp \
		sock_utils/sock_server.cpp \
		sock_utils/sock_utils.cpp
include $(BUILD_SHARED_LIBRARY)

include $(call all-makefiles-under, $(LOCAL_PATH))
