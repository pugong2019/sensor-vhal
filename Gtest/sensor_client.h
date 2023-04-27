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
#include <iostream>
#include <memory>
#include <string>
#include "sock_utils.h"
#include "sensors_vhal.h"

#define LOCAL_VHAL_IP "127.0.0.1"

using namespace std;
class SensorClient {
public:
    SensorClient();
    ~SensorClient();
    bool is_connected();
    int get_sensor_num();
    int is_acc_default_enabled() { return m_acc_enabled; };

private:
    void vhal_connected_callback(SockClient *sock);
    void vhal_disconnected_callback(SockClient *sock)
    void vhal_message_callback(SockClient* client);
    bool m_connected = false;
    bool m_acc_enabled = false;
    int m_sensor_num = 0;
    SockClient* m_client_sock = nullptr;
};
