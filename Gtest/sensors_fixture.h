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

#ifndef _SENSOR_FIXTURE_H
#define _SENSOR_FIXTURE_H

#include "gtest/gtest.h"
#include "sensors_client.h"
#include "sensors_helper.h"

#define LOG_TAG "SensorsFixture"

class SensorsFixture : public ::testing::Test {
public:
    SensorsClient m_sensors_client;
    SensorsHelper m_sensors_helper;
    SensorDevice m_sensor_dev;
};

#endif