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

#include <thread>
#include <chrono>
#include "SensorFixture.h"

#define SUPPORTED_SENSORS_NUMBER 9
#define INVALID_SENSOR_TYPE     -1

TEST_F(SensorFixture, SocketConnectionCheck)
{
    this_thread::sleep_for(std::chrono::microseconds(2000));
    ASSERT_TRUE(m_sensor_client.is_connected());
}

TEST_F(SensorFixture, SupportedSensorsNumberCheck)
{
    this_thread::sleep_for(std::chrono::microseconds(1000));
    ASSERT_EQ(SUPPORTED_SENSORS_NUMBER, m_sensors_helper.get_supported_sensors_num()); // Server Check
    ASSERT_EQ(SUPPORTED_SENSORS_NUMBER, m_sensor_client.get_sensor_num());  //Client Check
}

TEST_F(SensorFixture, SensorsTypeCheck)
{
    this_thread::sleep_for(std::chrono::microseconds(1000));
    ASSERT_FALSE(m_sensors_helper.is_supported_type(INVALID_SENSOR_TYPE));
    ASSERT_TRUE(m_sensors_helper.is_supported_type(SENSOR_TYPE_ACCELEROMETER));
    ASSERT_TRUE(m_sensors_helper.is_supported_type(SENSOR_TYPE_GYROSCOPE));
    ASSERT_TRUE(m_sensors_helper.is_supported_type(SENSOR_TYPE_MAGNETIC_FIELD));
    ASSERT_TRUE(m_sensors_helper.is_supported_type(SENSOR_TYPE_ACCELEROMETER_UNCALIBRATED));
    ASSERT_TRUE(m_sensors_helper.is_supported_type(SENSOR_TYPE_GYROSCOPE_UNCALIBRATED));
    ASSERT_TRUE(m_sensors_helper.is_supported_type(SENSOR_TYPE_MAGNETIC_FIELD_UNCALIBRATED));
    ASSERT_TRUE(m_sensors_helper.is_supported_type(SENSOR_TYPE_LIGHT));
    ASSERT_TRUE(m_sensors_helper.is_supported_type(SENSOR_TYPE_PROXIMITY));
    ASSERT_TRUE(m_sensors_helper.is_supported_type(SENSOR_TYPE_AMBIENT_TEMPERATURE));
}

TEST_F(SensorFixture, AccelerometerDefaultEnabledCheck)
{
    this_thread::sleep_for(std::chrono::microseconds(1000));
    ASSERT_TRUE(m_sensor_client.is_acc_default_enabled());
}