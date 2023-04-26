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

#include <iostream>
#include <log/log.h>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "sock_utils.h"
#include "sensors_vhal.h"

#define LOCAL_VHAL_IP "127.0.0.1"

using namespace std;
class SensorClient {
public:
    SensorClient();
    ~SensorClient();
    int startDummyStreamer();
    void stopDummyStreamer();
    bool is_connected();
    int get_sensor_num();
    int acc_sensor_open();

private:
    void vhal_connected_callback(SockClient *sock);
    void vhal_listener_handler(SockClient* client);
    bool is_running;
    bool m_connected = false;
    int m_sensor_num = 0;
    SockClient* m_client_sock = nullptr;
};
