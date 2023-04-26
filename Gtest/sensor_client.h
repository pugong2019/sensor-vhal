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

typedef enum SENSOR_TYPE {
    ACG_SENSOR_TYPE_INVALID                     = -1,
    ACG_SENSOR_TYPE_ACCELEROMETER               = 1,
    ACG_SENSOR_TYPE_MAGNETIC_FIELD              = 2,
    ACG_SENSOR_TYPE_GYROSCOPE                   = 4,
    ACG_SENSOR_TYPE_LIGHT                       = 5,
    ACG_SENSOR_TYPE_PRESSURE                    = 6,
    ACG_SENSOR_TYPE_PROXIMITY                   = 8,
    ACG_SENSOR_TYPE_GRAVITY                     = 9,
    ACG_SENSOR_TYPE_LINEAR_ACCELERATION         = 10,
    ACG_SENSOR_TYPE_ROTATION_VECTOR             = 11,
    ACG_SENSOR_TYPE_RELATIVE_HUMIDITY           = 12,
    ACG_SENSOR_TYPE_AMBIENT_TEMPERATURE         = 13,
    ACG_SENSOR_TYPE_MAGNETIC_FIELD_UNCALIBRATED = 14,
    ACG_SENSOR_TYPE_GAME_ROTATION_VECTOR        = 15,
    ACG_SENSOR_TYPE_GYROSCOPE_UNCALIBRATED      = 16,
    ACG_SENSOR_TYPE_SIGNIFICANT_MOTION          = 17,
    ACG_SENSOR_TYPE_STEP_DETECTOR               = 18,
    ACG_SENSOR_TYPE_STEP_COUNTER                = 19,
    ACG_SENSOR_TYPE_GEOMAGNETIC_ROTATION_VECTOR = 20,
    ACG_SENSOR_TYPE_HEART_RATE                  = 21,
    ACG_SENSOR_TYPE_POSE_6DOF                   = 28,
    ACG_SENSOR_TYPE_STATIONARY_DETECT           = 29,
    ACG_SENSOR_TYPE_MOTION_DETECT               = 30,
    ACG_SENSOR_TYPE_HEART_BEAT                  = 31,
    ACG_SENSOR_TYPE_ADDITIONAL_INFO             = 33,
    ACG_SENSOR_TYPE_LOW_LATENCY_OFFBODY_DETECT  = 34,
    ACG_SENSOR_TYPE_ACCELEROMETER_UNCALIBRATED  = 35
} SENSOR_TYPE_T;

using namespace std;
class SensorClient {
public:
    SensorClient();
    ~SensorClient();
    int startDummyStreamer();
    void stopDummyStreamer();
    bool is_connected();
    int get_sensor_num();
    int is_acc_enabled() { return m_acc_enabled;};

private:
    void vhal_connected_callback(SockClient *sock);
    void vhal_listener_handler(SockClient* client);
    bool is_running;
    bool m_connected = false;
    bool m_acc_enabled = false;

    int m_sensor_num = 0;
    SockClient* m_client_sock = nullptr;
};
