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
#define ENABLED                  1
#define DISABLED                 0
#define UNUSED_FLAG             -1
#define SAMPLE_PERIOD_NS         50*1000000
#define AT_LEAST_ONE_EVENT       1
#define SUCCESS                  0
#define FAILED                   -EINVAL

#define LOG_TAG "SensorsFixture"

extern int sensor_poll_events(struct sensors_poll_device_t* dev0, sensors_event_t* data, int count);
extern int sensor_activate(struct sensors_poll_device_t* dev0, int handle, int enabled);
extern int sensor_batch(struct sensors_poll_device_1* dev0, int handle, int flags __unused, \
                        int64_t sampling_period_ns, int64_t max_report_latency_ns __unused);
extern int sensor_set_delay(struct sensors_poll_device_t* dev0, int handle __unused, int64_t ns);
extern int sensor_flush(struct sensors_poll_device_1* dev0, int handle);
extern int sensor_close(struct hw_device_t* dev0);

SensorsFixture::SensorsFixture() {
    m_test_dev = new SensorDevice();
}

SensorsFixture::~SensorsFixture() {
    if(m_test_dev) {
        delete m_test_dev;
        m_test_dev = nullptr;
    }
}

TEST_F(SensorsFixture, SocketConnectionCheck)
{
    this_thread::sleep_for(std::chrono::milliseconds(2000)); //Make sure client connected to server
    ASSERT_TRUE(m_sensors_client.is_connected());
}

TEST_F(SensorsFixture, SupportedSensorsNumberCheck)
{
    ASSERT_EQ(SUPPORTED_SENSORS_NUMBER, m_sensors_helper.get_supported_sensors_num()); // Server Check
}

TEST_F(SensorsFixture, SensorsTypeCheck)
{
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
    this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_TRUE(m_sensors_client.is_acc_default_enabled());
}

TEST_F(SensorsFixture, ActivateMagneticFiledEvents)
{
    ASSERT_EQ(FAILED, sensor_activate((sensors_poll_device_t*)m_test_dev, INVALID_SENSOR_HANDLE, ENABLED));
    ASSERT_EQ(SUCCESS, sensor_activate((sensors_poll_device_t*)m_test_dev, ID_MAGNETIC_FIELD, ENABLED));
}

TEST_F(SensorsFixture, BatchMagneticFiledEvents)
{
    ASSERT_EQ(FAILED, sensor_batch((struct sensors_poll_device_1*)m_test_dev, INVALID_SENSOR_HANDLE, UNUSED_FLAG, SAMPLE_PERIOD_NS, UNUSED_FLAG));
    ASSERT_EQ(SUCCESS, sensor_batch((struct sensors_poll_device_1*)m_test_dev, ID_MAGNETIC_FIELD, UNUSED_FLAG, SAMPLE_PERIOD_NS, UNUSED_FLAG));
}

TEST_F(SensorsFixture, SetDelayMagneticFiledEvents)
{
    ASSERT_EQ(FAILED, sensor_set_delay((sensors_poll_device_t*)m_test_dev, INVALID_SENSOR_HANDLE, SAMPLE_PERIOD_NS));
    ASSERT_EQ(SUCCESS, sensor_set_delay((sensors_poll_device_t*)m_test_dev, ID_MAGNETIC_FIELD, SAMPLE_PERIOD_NS));
}

TEST_F(SensorsFixture, PollEventsWithZeroCount)
{
    ASSERT_EQ(SUCCESS, sensor_activate((sensors_poll_device_t*)m_test_dev, ID_MAGNETIC_FIELD, ENABLED));
    ASSERT_EQ(SUCCESS, sensor_batch((struct sensors_poll_device_1*)m_test_dev, ID_MAGNETIC_FIELD, UNUSED_FLAG, SAMPLE_PERIOD_NS, UNUSED_FLAG));
    ASSERT_EQ(FAILED, sensor_poll_events((sensors_poll_device_t*)m_test_dev, m_data, 0));
}

TEST_F(SensorsFixture, PollEventsWithNegativeCount)
{
    ASSERT_EQ(SUCCESS, sensor_activate((sensors_poll_device_t*)m_test_dev, ID_MAGNETIC_FIELD, ENABLED));
    ASSERT_EQ(SUCCESS, sensor_batch((struct sensors_poll_device_1*)m_test_dev, ID_MAGNETIC_FIELD, UNUSED_FLAG, SAMPLE_PERIOD_NS, UNUSED_FLAG));
    ASSERT_EQ(FAILED, sensor_poll_events((sensors_poll_device_t*)m_test_dev, m_data, -DATA_NUM));
}

TEST_F(SensorsFixture, PollEventsWithValidCount)
{
    ASSERT_EQ(SUCCESS, sensor_activate((sensors_poll_device_t*)m_test_dev, ID_MAGNETIC_FIELD, ENABLED));
    ASSERT_EQ(SUCCESS, sensor_batch((struct sensors_poll_device_1*)m_test_dev, ID_MAGNETIC_FIELD, UNUSED_FLAG, SAMPLE_PERIOD_NS, UNUSED_FLAG));
    ASSERT_EQ(AT_LEAST_ONE_EVENT, sensor_poll_events((sensors_poll_device_t*)m_test_dev, m_data, DATA_NUM));
}

TEST_F(SensorsFixture, FlushMagneticFiledEvents)
{
    ASSERT_EQ(SUCCESS, sensor_activate((sensors_poll_device_t*)m_test_dev, ID_MAGNETIC_FIELD, ENABLED));
    ASSERT_EQ(SUCCESS, sensor_batch((struct sensors_poll_device_1*)m_test_dev, ID_MAGNETIC_FIELD, UNUSED_FLAG, SAMPLE_PERIOD_NS, UNUSED_FLAG));
    ASSERT_LE(AT_LEAST_ONE_EVENT, sensor_poll_events((sensors_poll_device_t*)m_test_dev, m_data, DATA_NUM));
    ASSERT_EQ(SUCCESS, sensor_flush((struct sensors_poll_device_1*)m_test_dev, ID_MAGNETIC_FIELD));
}

TEST_F(SensorsFixture, SensorCloseCheck)
{
    ASSERT_EQ(SUCCESS, sensor_close((struct hw_device_t*)m_test_dev));
}
