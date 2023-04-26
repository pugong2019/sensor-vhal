#ifndef _SENSOR_FIXTUREL_H
#define _SENSOR_FIXTUREL_H

#include "gtest/gtest.h"
#include "sensor_client.h"

class SensorFixture : public ::testing::Test {
public:
    virtual void SetUp() {
        // Code here will be called immediately after the constructor (right
        // before each test).
        ALOGI("Call SensorFixture::SetUp()");
    }

    virtual void TearDown() {
        // Code here will be called immediately after each test (right
        // before the destructor).
        ALOGI("Call SensorFixture::TearDown()");
    }
public:
    SensorClient m_sensor_client;
};

#endif