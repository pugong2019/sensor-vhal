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

#ifndef _SENSORS_CLIENT_H
#define _SENSORS_CLIENT_H

#include <log/log.h>
#include "sensors_client.h"
#include "sock_utils.h"

#define LOG_TAG "SensorsClient"

using namespace std;
#define FAKA_DATA_LEN 20

SensorsClient::SensorsClient() {
    char buf[PROPERTY_VALUE_MAX] = {
        '\0',
    };
    int sensor_port = SENSOR_VHAL_PORT;
    if (property_get(SENSOR_VHAL_PORT_PROP, buf, NULL) > 0) {
        sensor_port = atoi(buf);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20)); //Make sure server init complete.
    std::string SocketPath;
    char build_id_buf[PROPERTY_VALUE_MAX] = {'\0'};
	property_get("ro.boot.container.id", build_id_buf, "");
    std::string sock_path = "/ipc/sensors-socket";
    sock_path.append(build_id_buf);
    char *k8s_env_value = getenv("K8S_ENV");
    SocketPath = (k8s_env_value != NULL && !strcmp(k8s_env_value, "true"))
			                ? "/conn/sensors-socket" : sock_path.c_str();
    int connection_type;
    if (property_get(SENSOR_SOCK_TYPE_PROP, buf, NULL) > 0) {
        if (!strcmp(buf, "INET")) {
            m_client_sock = new SockClient((char*)LOCAL_VHAL_IP, sensor_port, 1.0f);
            connection_type = SOCK_CONN_TYPE_INET_SOCK;
        } else {
            m_client_sock = new SockClient(SocketPath.c_str(), 1.0f);
            connection_type = SOCK_CONN_TYPE_UNIX_SOCK;
        }
    } else {
        m_client_sock = new SockClient(SocketPath.c_str(), 1.0f);
        connection_type = SOCK_CONN_TYPE_INET_SOCK;
    }
    m_client_sock->register_connected_callback(std::bind(&SensorsClient::vhal_connected_callback, this, std::placeholders::_1));
    m_client_sock->register_disconnected_callback(std::bind(&SensorsClient::vhal_disconnected_callback, this, std::placeholders::_1));
    m_client_sock->register_listener_callback(std::bind(&SensorsClient::vhal_message_callback, this, std::placeholders::_1));
    m_client_sock->start();
}

SensorsClient::~SensorsClient() {
    m_acc_enabled = false;
    m_connected = false;
    if(m_client_sock) {
        delete m_client_sock;
        m_client_sock = nullptr;
    }
}

void SensorsClient::vhal_connected_callback(SockClient *sock) {
    ALOGI("connected to server successfully: %s, %d", sock->get_ip(), sock->get_port());
    (void)(sock);
    m_connected = true;
}

void SensorsClient::vhal_disconnected_callback(SockClient *sock) {
    ALOGI("disconnected to server");
    (void)(sock);
    m_connected = false;
}

void SensorsClient::vhal_message_callback(SockClient* client) {
    sensor_config_msg_t sensor_ctrl_msg;
    char* pointer   = (char*)(&sensor_ctrl_msg);
    int len         = sizeof(sensor_config_msg_t);
    int retry_count = 30;
    int left_size   = len;
    while (left_size > 0) {
        int ret = client->recv_data(pointer, left_size);
        if (ret <= 0) {
            if (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN) {
                usleep(1000);
                if ((retry_count--) < 0) {
                    ALOGW("[timeout], failed to recv sensor config data from vhal:target: %d, recved len: %d, [%s], \n", len, len - left_size, strerror(errno));
                    return;
                }
                continue;
            } else {
                ALOGW("failed to recv sensor config data from vhal: %s\n", strerror(errno));
                return;
            }
        }
        left_size -= ret;
        pointer += ret;
    }

    ALOGI("receive config message from sensor vhal, sensor type: %d, enabled: %d, sample period: %d",
        sensor_ctrl_msg.sensor_type, sensor_ctrl_msg.enabled, sensor_ctrl_msg.sample_period);
    if(sensor_ctrl_msg.sensor_type == SENSOR_TYPE_ACCELEROMETER) {
        if(sensor_ctrl_msg.enabled) {
            m_acc_enabled = true;
        } else {
            m_acc_enabled = false;
        }
    }
    process_config_info(sensor_ctrl_msg);
}

int SensorsClient::get_payload_len(int sensor_type) {
    int payload_len = 0;
    switch (sensor_type) {
        case SENSOR_TYPE_ACCELEROMETER:
        case SENSOR_TYPE_GYROSCOPE:
        case SENSOR_TYPE_MAGNETIC_FIELD:
            payload_len = 3 * sizeof(float);
            break;
        case SENSOR_TYPE_ACCELEROMETER_UNCALIBRATED:
        case SENSOR_TYPE_GYROSCOPE_UNCALIBRATED:
        case SENSOR_TYPE_MAGNETIC_FIELD_UNCALIBRATED:
            payload_len = 6 * sizeof(float);
            break;
        case SENSOR_TYPE_LIGHT:
        case SENSOR_TYPE_PROXIMITY:
        case SENSOR_TYPE_AMBIENT_TEMPERATURE:
            payload_len = 1 * sizeof(float);
            break;
        default:
            payload_len = 0;
            ALOGW("unsupported sensor type %d", sensor_type);
            break;
    }
    return payload_len;
}


void SensorsClient::process_config_info(sensor_config_msg_t& cfg) {
    for(int i = 0; i < FAKA_DATA_LEN; i++) {
        aic_sensors_event_t *fake_data = nullptr;
        size_t pay_load_len = get_payload_len(cfg.sensor_type);
        size_t packet_size = sizeof(aic_sensors_event_t) + pay_load_len;
        fake_data = (aic_sensors_event_t *)new char[packet_size];
        memset(fake_data, 0, packet_size);  //Fill faka data. all default to 0
        fake_data->type = cfg.sensor_type;
        m_client_sock->send_data(fake_data, packet_size);
        usleep(cfg.sample_period * 1000);
        delete fake_data;
        fake_data = nullptr;
    }
}

#endif