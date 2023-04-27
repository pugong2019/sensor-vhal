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
#include "sensors_fixture.h"

#define SUPPORTED_SENSORS_NUMBER 9
#define INVALID_SENSOR_TYPE     -1
#define INVALID_SENSOR_HANDLE   -1
#define ENABLED  1
#define DISABLED 0
#define DATA_NUM 5

TEST_F(SensorsFixture, SocketConnectionCheck)
{
    this_thread::sleep_for(std::chrono::microseconds(2000));
    ASSERT_TRUE(m_sensors_client.is_connected());
}

TEST_F(SensorsFixture, SupportedSensorsNumberCheck)
{
    this_thread::sleep_for(std::chrono::microseconds(1000));
    ASSERT_EQ(SUPPORTED_SENSORS_NUMBER, m_sensors_helper.get_supported_sensors_num()); // Server Check
    ASSERT_EQ(SUPPORTED_SENSORS_NUMBER, m_sensors_client.get_sensors_num());  //Client Check
}

TEST_F(SensorsFixture, SensorsTypeCheck)
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

TEST_F(SensorsFixture, AccelerometerDefaultEnabledCheck)
{
    this_thread::sleep_for(std::chrono::microseconds(1000));
    ASSERT_TRUE(m_sensors_client.is_acc_default_enabled());
}

TEST_F(SensorsFixture, ActivateMagneticFiledEvents)
{
    this_thread::sleep_for(std::chrono::microseconds(1000));
    ASSERT_GT(0, m_sensor_dev.sensor_device_activate(INVALID_SENSOR_HANDLE, ENABLED));
    ASSERT_EQ(0, m_sensor_dev.sensor_device_activate(ID_MAGNETIC_FIELD, ENABLED));
}

TEST_F(SensorsFixture, BatchMagneticFiledEvents)
{
    this_thread::sleep_for(std::chrono::microseconds(1000));
    uint64_t sample_period = 50000000; //ns
    ASSERT_GT(0, m_sensor_dev.sensor_device_batch(INVALID_SENSOR_HANDLE, sample_period));
    ASSERT_EQ(0, m_sensor_dev.sensor_device_batch(ID_MAGNETIC_FIELD, sample_period));
}

TEST_F(SensorsFixture, SetDelayMagneticFiledEvents)
{
    this_thread::sleep_for(std::chrono::microseconds(1000));
    uint64_t sample_period = 50000000;
    ASSERT_GT(0, m_sensor_dev.sensor_device_set_delay(INVALID_SENSOR_HANDLE, sample_period));
    ASSERT_EQ(0, m_sensor_dev.sensor_device_set_delay(ID_MAGNETIC_FIELD, sample_period));
}

TEST_F(SensorsFixture, PollEvents)
{
    this_thread::sleep_for(std::chrono::microseconds(1000));
    sensors_event_t data[DATA_NUM];
    ASSERT_EQ(DATA_NUM, m_sensor_dev.sensor_device_poll(data, DATA_NUM));
}

