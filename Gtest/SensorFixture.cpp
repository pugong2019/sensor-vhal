#include <thread>
#include <chrono>
#include "SensorFixture.h"

#define LOG_TAG "SensorFixture"

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
    this_thread::sleep_for(2000ms);
    ASSERT_TRUE(m_sensor_client.is_connected());
}