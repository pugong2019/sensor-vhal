#
# Copyright 2016 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#
ifeq ($(USE_SENSOR_SOCKET_UTILS), true)
LOCAL_PATH := $(call my-dir)

#####################libsock_util###########################
include $(CLEAR_VARS)

LOCAL_CFLAGS := -g -DLOG_TAG=\"SensorsSock\"
LOCAL_LDFLAGS := -g

LOCAL_C_INCLUDES := $(LOCAL_PATH)/include

LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/include

# to support platfrom build system
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_EXPORT_C_INCLUDES)

LOCAL_SRC_FILES:= \
    sock_server.cpp \
    sock_client.cpp

LOCAL_LDLIBS := -llog

LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE := libsock_util
include $(BUILD_SHARED_LIBRARY)

include $(call all-makefiles-under, $(LOCAL_PATH))
endif

