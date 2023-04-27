#include <thread>
#include <chrono>
#include "SensorFixture.h"

#define SUPPORTED_SENSORS_NUMBER 9

TEST_F(SensorFixture, SocketConnectionCheck)
{
    this_thread::sleep_for(std::chrono::microseconds(2000));
    ASSERT_TRUE(m_sensor_client.is_connected());
}

TEST_F(SensorFixture, SensorsNumberCheck)
{
    this_thread::sleep_for(std::chrono::microseconds(1000));
    ASSERT_EQ(SUPPORTED_SENSORS_NUMBER, m_sensor_client.get_sensor_num());
}

TEST_F(SensorFixture, AccelerometerDefaultEnabledCheck)
{
    this_thread::sleep_for(std::chrono::microseconds(1000));
    ASSERT_TRUE(m_sensor_client.is_acc_default_enabled());
}