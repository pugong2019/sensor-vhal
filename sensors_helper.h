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

#ifndef _SENSORS_HELPER_H
#define _SENSORS_HELPER_H

#include <iostream>
#include <string>
#include "sensors_vhal.h"

class SensorsHelper {
public:
    SensorsHelper() { };
    ~SensorsHelper() { };

    int get_supported_sensors_num() {
        return MAX_NUM_SENSORS;
    }
    bool is_supported_type(int sensor_type) {
        switch (sensor_type) {
            case SENSOR_TYPE_ACCELEROMETER:
            case SENSOR_TYPE_GYROSCOPE:
            case SENSOR_TYPE_MAGNETIC_FIELD:
            case SENSOR_TYPE_ACCELEROMETER_UNCALIBRATED:
            case SENSOR_TYPE_GYROSCOPE_UNCALIBRATED:
            case SENSOR_TYPE_MAGNETIC_FIELD_UNCALIBRATED:
            case SENSOR_TYPE_LIGHT:
            case SENSOR_TYPE_PROXIMITY:
            case SENSOR_TYPE_AMBIENT_TEMPERATURE:
                return true;
            default:
                return false;
        }
    }
};

#endif

