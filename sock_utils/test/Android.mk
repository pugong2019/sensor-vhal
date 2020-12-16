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

LOCAL_PATH := $(call my-dir)

##################test###########################
include $(CLEAR_VARS)

LOCAL_CFLAGS := -g -DLOG_TAG=\"test_sock_server\"
LOCAL_LDFLAGS := -g

LOCAL_C_INCLUDES := $(LOCAL_PATH)/../include \
                    $(LOCAL_PATH)/../../include \

LOCAL_SRC_FILES := \
   test_sock_server.cpp

LOCAL_SHARED_LIBRARIES := \
   libsock_util

LOCAL_LDLIBS := -llog

LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE := test_sock_server
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)


###################test###########################
include $(CLEAR_VARS)

LOCAL_CFLAGS := -g -DLOG_TAG=\"test_sock_client\" -Wno-unused-parameter
LOCAL_LDFLAGS := -g

LOCAL_C_INCLUDES := $(LOCAL_PATH)/../include \
                    $(LOCAL_PATH)/../../include \

LOCAL_SRC_FILES := \
    test_sock_client.cpp

LOCAL_SHARED_LIBRARIES := \
    libsock_util
LOCAL_LDLIBS := -llog

LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE := test_sock_client
include $(BUILD_EXECUTABLE)


####################test_host###########################
include $(CLEAR_VARS)

LOCAL_CFLAGS := -g -DLOG_TAG=\"sock_util\"
LOCAL_LDFLAGS := -g

LOCAL_C_INCLUDES := $(LOCAL_PATH)/../include \
                    $(LOCAL_PATH)/../../include \

LOCAL_SRC_FILES := \
    test_sock_server.cpp \
    ../sock_server.cpp

LOCAL_SHARED_LIBRARIES :=
LOCAL_LDLIBS := -llog

LOCAL_MODULE := test_host_server
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)


####################test_host###########################
include $(CLEAR_VARS)

LOCAL_CFLAGS := -g -DLOG_TAG=\"sock_util\" 

LOCAL_LDFLAGS := -g

LOCAL_C_INCLUDES := $(LOCAL_PATH)/../include \
                    $(LOCAL_PATH)/../../include \

$(info $(LOCAL_PATH))
LOCAL_SRC_FILES := \
    test_sock_client.cpp \
    ../sock_client.cpp

LOCAL_SHARED_LIBRARIES :=
LOCAL_LDLIBS := -llog

LOCAL_MODULE := test_host_client
include $(BUILD_EXECUTABLE)
