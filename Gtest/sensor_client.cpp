/*
 * Copyright (C) 2023 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <log/log.h>
#include "sensor_client.h"
#include "sock_utils.h"

#define LOG_TAG "SensorClient"

using namespace std;

SensorClient::SensorClient() {
    char buf[PROPERTY_VALUE_MAX] = {
        '\0',
    };
    int sensor_port = SENSOR_VHAL_PORT;
    if (property_get(SENSOR_VHAL_PORT_PROP, buf, NULL) > 0) {
        sensor_port = atoi(buf);
    }

    std::string SocketPath;
    char build_id_buf[PROPERTY_VALUE_MAX] = {'\0'};
	property_get("ro.boot.container.id", build_id_buf, "");
    std::string sock_path = "/ipc/sensors-socket";
    sock_path.append(build_id_buf);
    char *k8s_env_value = getenv("K8S_ENV");
    SocketPath = (k8s_env_value != NULL && !strcmp(k8s_env_value, "true"))
			                ? "/conn/sensors-socket" : sock_path.c_str();

    if (property_get(SENSOR_SOCK_TYPE_PROP, buf, NULL) > 0) {
        if (!strcmp(buf, "INET")) {
            m_client_sock = new SockClient((char*)LOCAL_VHAL_IP, sensor_port);
            ALOGI("SensorClient: LOCAL_VHAL_IP = %s, sensor_port = %d", LOCAL_VHAL_IP, sensor_port);

        } else {
            m_client_sock = new SockClient(SocketPath.c_str());
            ALOGI("SensorClient: LOCAL_VHAL_IP = %s UNIX Type", SocketPath.c_str());
        }
    } else {
        m_client_sock = new SockClient(SocketPath.c_str());
        ALOGI("SensorClient: LOCAL_VHAL_IP = %s UNIX Type", SocketPath.c_str());
    }

    m_client_sock->register_connected_callback(std::bind(&SensorClient::vhal_connected_callback, this, std::placeholders::_1));
    m_client_sock->start();
}


SensorClient::~SensorClient() {
    if(m_client_sock) {
        delete m_client_sock;
        m_client_sock = nullptr;
    }
}

bool SensorClient::is_connected() {
    return m_connected;
}

void SensorClient::vhal_connected_callback(SockClient *sock) {
    ALOGI("connected to server successfully");
    (void)(sock);
    m_connected = true;
}

