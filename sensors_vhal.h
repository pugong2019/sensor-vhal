#ifndef _SENSORS_VHAL_H
#define _SENSORS_VHAL_H
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <log/log.h>
#include <cutils/sockets.h>
#include <hardware/sensors.h>
#include <time.h>
#include <cutils/properties.h>
#include <pthread.h>

#define  SENSORS_SERVICE_NAME "sensors"
#define MAX_NUM_SENSORS 3
#define SUPPORTED_SENSORS  ((1<<MAX_NUM_SENSORS)-1)

/**  SENSOR IDS AND NAMES **/
#define  ID_BASE                        SENSORS_HANDLE_BASE
#define  ID_ACCELERATION                (ID_BASE+0)
#define  ID_GYROSCOPE                   (ID_BASE+1)
#define  ID_MAGNETIC_FIELD              (ID_BASE+2)
#define  ID_ORIENTATION                 (ID_BASE+3)
#define  ID_TEMPERATURE                 (ID_BASE+4)
#define  ID_PROXIMITY                   (ID_BASE+5)
#define  ID_LIGHT                       (ID_BASE+6)
#define  ID_PRESSURE                    (ID_BASE+7)
#define  ID_HUMIDITY                    (ID_BASE+8)
#define  ID_MAGNETIC_FIELD_UNCALIBRATED (ID_BASE+9)

#define  SENSORS_ACCELERATION                 (1 << ID_ACCELERATION)
#define  SENSORS_GYROSCOPE                    (1 << ID_GYROSCOPE)
#define  SENSORS_MAGNETIC_FIELD               (1 << ID_MAGNETIC_FIELD)
#define  SENSORS_ORIENTATION                  (1 << ID_ORIENTATION)
#define  SENSORS_TEMPERATURE                  (1 << ID_TEMPERATURE)
#define  SENSORS_PROXIMITY                    (1 << ID_PROXIMITY)
#define  SENSORS_LIGHT                        (1 << ID_LIGHT)
#define  SENSORS_PRESSURE                     (1 << ID_PRESSURE)
#define  SENSORS_HUMIDITY                     (1 << ID_HUMIDITY)
#define  SENSORS_MAGNETIC_FIELD_UNCALIBRATED  (1 << ID_MAGNETIC_FIELD_UNCALIBRATED)

#define SENSOR_VHAL_PORT_PROP      "virtual.sensor.tcp.port"
#define SENSOR_VHAL_PORT           8772

#define DEBUG_OPTION false

typedef struct {
    union {
        struct {
            float x;
            float y;
            float z;
        };
        struct {
            float azimuth;
            float pitch;
            float roll;
        };
    };
} acgmsg_sensors_vec_t;

// ACG_MESSAGE_CLIENT_SENSOR payload
//now AIC only support acceleration, magnetic, gyro
typedef struct acgmsg_sensors_event_t {
    /* sensor type */
    int32_t type; // acgmsg_sensor_type_t
    /* time is in nanosecond */
    int64_t timestamp;
    union {
        union {
            /* acceleration values are in meter per second per second (m/s^2) */
            acgmsg_sensors_vec_t   acceleration;
            /* magnetic vector values are in micro-Tesla (uT) */
            acgmsg_sensors_vec_t   magnetic;
            /* orientation values are in degrees */
            acgmsg_sensors_vec_t   orientation;
            /* gyroscope values are in rad/s */
            acgmsg_sensors_vec_t   gyro;
            /* temperature is in degrees centigrade (Celsius) */
            float           temperature;
            /* distance in centimeters */
            float           distance;
            /* light in SI lux units */
            float           light;
            /* pressure in hectopascal (hPa) */
            float           pressure;
            /* relative humidity in percent */
            float           relative_humidity;
        };
    };
} acgmsg_sensors_event_t;

#define  SENSORS_LIST  \
    SENSOR_(ACCELERATION,"acceleration") \
    SENSOR_(GYROSCOPE,"gyroscope") \
    SENSOR_(MAGNETIC_FIELD,"magnetic-field") \

static const struct {
    const char*  name;
    int          id; } _sensorIds[MAX_NUM_SENSORS] =
{
#define SENSOR_(x,y)  { y, ID_##x },
    SENSORS_LIST
#undef  SENSOR_
};

#endif
