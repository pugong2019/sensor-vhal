#include <thread>
#include <chrono>
#include "SensorFixture.h"

#define LOG_TAG "SensorFixture"
#define SENSORS_NUMBER 9

// class SensorFixture : public ::testing::Test {
// public:
//     virtual void SetUp() {
//         // Code here will be called immediately after the constructor (right
//         // before each test).
//     }

//     virtual void TearDown() {
//         // Code here will be called immediately after each test (right
//         // before the destructor).
//     }
// public:
//     SensorClient m_sensor_client;
// };

// void SensorFixture::SetUp()
// {
//     ALOGI("Call SensorFixture");
// }

// void SensorFixture::TearDown()
// {
//     ALOGI("Call TearDown");
// }

TEST_F(SensorFixture, SocketConnectionCheck)
{
    this_thread::sleep_for(std::chrono::microseconds(2000));
    ASSERT_TRUE(m_sensor_client.is_connected());
}

TEST_F(SensorFixture, SensorsNumberCheck)
{
    this_thread::sleep_for(std::chrono::microseconds(1000));
    ASSERT_EQ(SENSORS_NUMBER, m_sensor_client.get_sensor_num());
}