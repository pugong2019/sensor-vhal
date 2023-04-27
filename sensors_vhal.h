#ifndef _SENSORS_VHAL_H
#define _SENSORS_VHAL_H
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <vector>
#include <log/log.h>
#include <queue>
#include <vector>
#include <condition_variable>
#include <cutils/sockets.h>
#include <hardware/sensors.h>
#include <time.h>
#include <cutils/properties.h>
#include <pthread.h>
#include "sock_utils.h"

#define  SENSOR_SOCK_TYPE_PROP "ro.vendor.sensors.sock.type"
#define  SENSORS_SERVICE_NAME "sensors"
#define  MAX_NUM_SENSORS 9
#define  SUPPORTED_SENSORS  ((1<<MAX_NUM_SENSORS)-1)

/**  SENSOR IDS AND NAMES **/
#define  ID_BASE                        SENSORS_HANDLE_BASE
#define  ID_ACCELEROMETER               (ID_BASE+0)
#define  ID_GYROSCOPE                   (ID_BASE+1)
#define  ID_MAGNETIC_FIELD              (ID_BASE+2)
#define  ID_ACCELEROMETER_UNCALIBRATED  (ID_BASE+3)
#define  ID_GYROSCOPE_UNCALIBRATED      (ID_BASE+4)
#define  ID_MAGNETIC_FIELD_UNCALIBRATED (ID_BASE+5)
#define  ID_LIGHT                       (ID_BASE+6)
#define  ID_PROXIMITY                   (ID_BASE+7)
#define  ID_TEMPERATURE                 (ID_BASE+8)


// #define  ID_LIGHT                       (ID_BASE+6)
// #define  ID_PRESSURE                    (ID_BASE+7)
// #define  ID_HUMIDITY                    (ID_BASE+8)

#define  SENSORS_ACCELEROMETER                (1 << ID_ACCELEROMETER)
#define  SENSORS_GYROSCOPE                    (1 << ID_GYROSCOPE)
#define  SENSORS_MAGNETIC_FIELD               (1 << ID_MAGNETIC_FIELD)
#define  SENSORS_ACCELEROMETER_UNCALIBRATED   (1 << ID_ACCELEROMETER_UNCALIBRATED)
#define  SENSORS_GYROSCOPE_UNCALIBRATED       (1 << ID_GYROSCOPE_UNCALIBRATED)
#define  SENSORS_MAGNETIC_FIELD_UNCALIBRATED  (1 << ID_MAGNETIC_FIELD_UNCALIBRATED)
#define  SENSORS_LIGHT                        (1 << ID_LIGHT)
#define  SENSORS_PROXIMITY                    (1 << ID_PROXIMITY)
#define  SENSORS_TEMPERATURE                  (1 << ID_TEMPERATURE)

#define SENSOR_VHAL_PORT_PROP      "virtual.sensor.tcp.port"
#define SENSOR_VHAL_PORT           8772
#define MAX_MSG_QUEUE_SIZE         128
#define MEM_POOL_SIZE              16
#define MAX_SENSOR_PAYLOAD_SIZE    (sizeof(float)*8)

#define SYS_VHAL_PROP_LOG_TRACE_COUNT "sys.sensor.log_trace.interval"
typedef struct {
    int32_t    sensor_type;       // acgmsg_sensor_type_t
    int32_t    enabled;       // acgmsg_sensor_status_t for cmd: ACG_SENSOR_ACTIVATE
    int32_t    sample_period; // ACG_SENSOR_BATCH
} sensor_config_msg_t;


int test_num = 10;
// snesor payload
//now AIC only support acceleration, magnetic, gyro
/*
| sensor type       | data type | data len | detail                                           | note                                                         | status        |
| :---------------- | :-------: | :------: | :----------------------------------------------- | :----------------------------------------------------------- | :------------ |
| acceleremoter     |   float   |    3     | data[0]=acc.x,  data[1]=acc.y,  data[2]=acc.z    | acceleration values are in meter per second per second (m/s^2) | supported     |
| magnetic-filed    |   float   |    3     | data[0]=mag.x, data[1]=mag.y,  data[2]=mag.z     | magnetic vector values are in micro-Tesla (uT)               | supported     |
| gyroscope         |   float   |    3     | data[0]=gyro.x,  data[1]=gyro.y,  data[2]=gyro.z | gyroscope values are in rad/s                                | supported     |
| temperature       |   float   |    1     | data=temperature_value                           | temperature is in degrees centigrade (Celsius)               | not supported |
| distance          |   float   |    1     | data=distance_value                              | distance in centimeters                                      | not supported |
| light             |   float   |    1     | data=light_value                                 | light in SI lux units                                        | not supported |
| pressure          |   float   |    1     | data=pressure_value                              | pressure in hectopascal (hPa)                                | not supported |
| relative_humidity |   float   |    1     | data=relative_humidity_value                     | relative humidity in percent                                 | not supported |
|                   |           |          |                                                  |                                                              |               |
 *
 */
typedef struct _aic_sensors_event_t {
    /* sensor type */
    int32_t   type; // acgmsg_sensor_type_t
    int32_t   data_num;
    /* time is in nanosecond */
    int64_t   timestamp;
    union
    {
        float   fdata[0];
        int32_t idata[0];
        char    cdata[0];
    }data;
} aic_sensors_event_t;

#define  SENSORS_LIST  \
    SENSOR_(ACCELEROMETER,"acceleration") \
    SENSOR_(GYROSCOPE,"gyroscope") \
    SENSOR_(MAGNETIC_FIELD,"magnetic-field") \
    SENSOR_(ACCELEROMETER_UNCALIBRATED,"acceleration_uncalibrated") \
    SENSOR_(GYROSCOPE_UNCALIBRATED,"gyroscope_uncalibrated") \
    SENSOR_(MAGNETIC_FIELD_UNCALIBRATED,"magnetic-field_uncalibrated") \
    SENSOR_(LIGHT,"light") \
    SENSOR_(PROXIMITY,"proximity") \
    SENSOR_(TEMPERATURE,"temperature") \

static const struct {
    const char*  name;
    int          id; } _sensorIds[MAX_NUM_SENSORS] =
{
#define SENSOR_(x,y)  { y, ID_##x },
    SENSORS_LIST
#undef  SENSOR_
};

class SensorDevice {
public:
    struct sensors_poll_device_1 device;  // must be first
    SensorDevice();
    ~SensorDevice();
    int sensor_device_poll(sensors_event_t* data, int count);
    int sensor_device_activate(int handle, int enabled);
    int sensor_device_flush(int handle);
    int sensor_device_set_delay(int handle, int64_t ns);
    int sensor_device_batch(int sensor_handle, int64_t sampling_period_ns);

private:
    uint32_t m_flush_count[MAX_NUM_SENSORS];
    uint32_t m_pending_sensors;
    int64_t m_time_start;
    int64_t m_time_offset;
    int64_t m_log_trace_count;

    sensors_event_t m_sensors[MAX_NUM_SENSORS];
    SockServer* m_socket_server;
    sensor_config_msg_t m_sensor_config_status[MAX_NUM_SENSORS];
    std::mutex m_msg_queue_mtx;
    std::mutex m_msg_pool_mtx;
    std::mutex m_mutex;
    std::condition_variable m_msg_queue_ready_cv;
    std::queue<std::unique_ptr<std::vector<char>>> m_msg_mem_pool;
    std::queue<std::unique_ptr<std::vector<char>>> m_sensor_msg_queue;

private:
    int64_t now_ns(void);
    int get_type_from_hanle(int handle);
    int sensor_device_poll_event_locked();
    int sensor_device_send_config_msg(const void* cmd, size_t len);
    int sensor_device_pick_pending_event_locked(sensors_event_t* event);
    void sensor_event_callback(SockServer* sock, sock_client_proxy_t* client);
    void client_connected_callback(SockServer* sock, sock_client_proxy_t* client);
    int get_handle_from_type(int sensor_type);
    int get_payload_len(int sensor_type);
    const char* get_name_from_handle(int id);
};

#endif
