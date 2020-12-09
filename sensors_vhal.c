/*
 * Copyright (C) 2009 The Android Open Source Project
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
 */

/* this implements a sensors hardware library for the Android emulator.
 * the following code should be built as a shared library that will be
 * placed into /system/lib/hw/sensors.goldfish.so
 *
 * it will be loaded by the code in hardware/libhardware/hardware.c
 * which is itself called from com_android_server_SensorService.cpp
 */


/* we connect with the emulator through the "sensors" qemud service
 */
#define  SENSORS_SERVICE_NAME "sensors"

// #define LOG_TAG "QemuSensors"

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <log/log.h>
#include <cutils/sockets.h>
#include <hardware/sensors.h>
#include <time.h>
#include <cutils/properties.h>

/** SENSOR IDS AND NAMES
 **/

#define MAX_NUM_SENSORS 3

#define SUPPORTED_SENSORS  ((1<<MAX_NUM_SENSORS)-1)

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

static const char* get_name_from_handle( int  id )
{
    int  nn;
    for (nn = 0; nn < MAX_NUM_SENSORS; nn++)
        if (id == _sensorIds[nn].id)
            return _sensorIds[nn].name;
    return "<UNKNOWN>";
}

/* return the current time in nanoseconds */
static int64_t now_ns(void) {
    struct timespec  ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static __inline__ int
create_server_socket() {
    static int server_fd = -1, client_fd = -1;
    ALOGD("setup sensors vhal server socket ...");
    char buf[PROPERTY_VALUE_MAX] = {
            '\0',
    };
    int virtual_sensor_port = SENSOR_VHAL_PORT;
    if (property_get(SENSOR_VHAL_PORT_PROP, buf, NULL) > 0) {
        virtual_sensor_port = atoi(buf);
    }

    if(server_fd == -1 || client_fd == -1){
        int enable = 1;
        struct	sockaddr_in server_addr;
        server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        if(server_fd < 0){
            ALOGE("create socket failed!");
            return -1;
        }
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(virtual_sensor_port);
        server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
            ALOGE("[err] server_fd reuse");
            close(server_fd);
        }

        if(bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
            ALOGE("bind failed: %s", strerror(errno));
            close(server_fd);
            return -1;
        }

        ALOGD("sensors vhal start to listen [%d]", virtual_sensor_port);
        if(listen(server_fd, 20) == -1) {
            ALOGE("listen failed");
            close(server_fd);
            return -1;
        }

        struct  sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        ALOGD("waiting for client connected...");
        if( (client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len)) == -1) { 
            ALOGD("accept failed: %s", strerror(errno));
            close(server_fd);
            return -1;
        }
        ALOGD("client connected successfully");
    }
    return client_fd;
}

static __inline__ int
sensors_vhal_sock_send(int fd, const void*  msg, int  msglen) {
    if (msglen < 0)
        msglen = strlen((const char*)msg);

    if (msglen == 0)
        return 0;
    int wr_len = write(fd, msg, msglen);
    return wr_len;
}

static __inline__ int
sensors_vhal_sock_recv(int fd, void* msg, int msgsize) {
    size_t left_size = msgsize;
    int total_size = 0;
    uint8_t *ptr = msg;
    while (left_size > 0){
        int ret = read(fd, ptr, left_size);
        if(ret < 0){
            return -1;
        }
        ptr += ret;
        left_size -= ret;
        total_size += ret;
    }
    return total_size;
}

#define CMD_SENSOR_BATCH      0x11
#define CMD_SENSOR_ACTIVATE   0x22

typedef struct
{
    int32_t    cmd_type;         // acgmsg_sensor_conig_type_t
    int32_t    sensor_type;       // acgmsg_sensor_type_t
    union 
    {
       int32_t    enabled;       // acgmsg_sensor_status_t for cmd: ACG_SENSOR_ACTIVATE
       int32_t    sample_period; // ACG_SENSOR_BATCH
    };
} sensor_config_msg_t;

typedef struct SensorDevice {
    struct sensors_poll_device_1  device;
    sensors_event_t               sensors[MAX_NUM_SENSORS];
    uint32_t                      pending_sensors;
    int64_t                       time_start;
    int64_t                       time_offset;
    uint32_t                      active_sensors;
    int                           fd;
    int                           flush_count[MAX_NUM_SENSORS];
    pthread_mutex_t               lock;
} SensorDevice;

static int sensor_device_get_socket_fd() {
    /* Create connection to service on first call */
    int fd = create_server_socket();
    if (fd < 0) {
        int ret = -errno;
        ALOGE("could not build connection with sensor client: %s", strerror(-ret));
        return ret;
    }
    return fd;
}

/* Send a command to the sensors virtual device. |dev| is a device instance and
 * |cmd| is a zero-terminated command string. Return 0 on success, or -errno
 * on failure. */
static int sensor_device_send_config_msg(SensorDevice* dev, const void* cmd, size_t len) {
    int fd = dev->fd;
    if (fd < 0) {
        return -errno;
    }

    int ret = sensors_vhal_sock_send(fd, cmd, len);
    if (ret < 0) {
        ret = -errno;
        ALOGE("%s(fd=%d): ERROR: %s", __FUNCTION__, fd, strerror(errno));
    }
    return ret;
}

static int sensor_device_close(struct hw_device_t* dev0)
{
    SensorDevice* dev = (void*)dev0;
    // Assume that there are no other threads blocked on poll()
    if (dev->fd >= 0) {
        close(dev->fd);
        dev->fd = -1;
    }
    pthread_mutex_destroy(&dev->lock);
    free(dev);
    return 0;
}

static int get_type_from_hanle(int handle){
    int id = -1;
    switch (handle)
    {
    case ID_ACCELERATION:
        id = SENSOR_TYPE_ACCELEROMETER;
        break;
    case ID_GYROSCOPE:
        id = SENSOR_TYPE_GYROSCOPE;
        break;
    case ID_MAGNETIC_FIELD:
        id = SENSOR_TYPE_MAGNETIC_FIELD;
        break;
    default:
        ALOGE("unknown handle (%d)", handle);
        return -EINVAL;
    }
    return id;
}

/* Return an array of sensor data. This function blocks until there is sensor
 * related events to report. On success, it will write the events into the
 * |data| array, which contains |count| items. The function returns the number
 * of events written into the array, which shall never be greater than |count|.
 * On error, return -errno code.
 *
 * Note that according to the sensor HAL [1], it shall never return 0!
 */

static int sensor_device_poll_event_locked(SensorDevice* dev){
    int fd = dev->fd;
    if(dev->fd < 0){
        return -EINVAL;
    }
    char sensor_buf[256];
    static double lastAccTime = 0;
    static double lastGyroTime = 0;
    static double lastMagTime = 0;
    static int64_t acc_count = 0;
    static int64_t gyr_count = 0;
    static int64_t mag_count = 0;
    sensors_event_t new_sensor_events;
    int len = -1;
    uint32_t new_sensors = 0U;

    for(;;){ // make sure recv one event
        pthread_mutex_unlock(&dev->lock);
        len = sensors_vhal_sock_recv(fd, &new_sensor_events, sizeof(sensors_event_t)); //block mode,recv one event per time
        pthread_mutex_lock(&dev->lock);
        if (len < 0) {
            ALOGE("sensors vhal recv data failed: %s ", strerror(errno));
            return -errno;
        }
        switch (new_sensor_events.type)
        {
            case SENSOR_TYPE_ACCELEROMETER:
                new_sensors |= SENSORS_ACCELERATION;
                memcpy(dev->sensors+ID_ACCELERATION, &new_sensor_events, sizeof(sensors_event_t));
                acc_count++;
                if(acc_count%1000 == 0){
                    ALOGD("[%-5d] Acc: %f,%f,%f, time = %.3f ms", acc_count, new_sensor_events.acceleration.x, new_sensor_events.acceleration.y, new_sensor_events.acceleration.z, ((double)(new_sensor_events.timestamp-lastAccTime))/1000000.0);
                }
                lastAccTime = new_sensor_events.timestamp;
                break;

            case SENSOR_TYPE_GYROSCOPE:
                new_sensors |= SENSORS_GYROSCOPE;
                memcpy(dev->sensors+ID_GYROSCOPE, &new_sensor_events, sizeof(sensors_event_t));
                gyr_count++;
                if(gyr_count%1000 == 0){
                    ALOGD("[%-5d] Gyr: %f,%f,%f, time = %.3f ms", gyr_count, new_sensor_events.acceleration.x, new_sensor_events.acceleration.y, new_sensor_events.acceleration.z, ((double)(new_sensor_events.timestamp-lastAccTime))/1000000.0);
                }
                lastGyroTime = new_sensor_events.timestamp;
                break;

            case SENSOR_TYPE_MAGNETIC_FIELD:
                new_sensors |= SENSORS_MAGNETIC_FIELD;
                memcpy(dev->sensors+ID_MAGNETIC_FIELD, &new_sensor_events, sizeof(sensors_event_t));
                mag_count++;
                if(mag_count%1000 == 0){
                    ALOGD("[%-5d] Mag: %f,%f,%f, time = %.3f ms", mag_count, new_sensor_events.acceleration.x, new_sensor_events.acceleration.y, new_sensor_events.acceleration.z, ((double)(new_sensor_events.timestamp-lastAccTime))/1000000.0);
                }
                lastMagTime = new_sensor_events.timestamp;
                break;

            default:
                ALOGE("unsupported sensor type: %d, continuing to receive next event", new_sensor_events.type);
                continue;
        }
        break;
    }
   
    /* update the time of each new sensor event. let's compare the remote 
        * sensor timestamp with current time and take the lower value 
        * --- we don't believe in events from the future anyway.
    */
    if (new_sensors) {
        dev->pending_sensors |= new_sensors;
        int64_t remote_timestamp = new_sensor_events.timestamp;
        int64_t host_timestamp = now_ns();
        if (dev->time_start == 0) {
            dev->time_start  = host_timestamp;
            dev->time_offset = dev->time_start - remote_timestamp;
        }

        remote_timestamp += dev->time_offset;
        if (remote_timestamp > host_timestamp) {
            remote_timestamp = host_timestamp;
        }
        while (new_sensors) {
            uint32_t i = 31 - __builtin_clz(new_sensors);
            new_sensors &= ~(1U << i);
            dev->sensors[i].timestamp = remote_timestamp;
        }
    }
    return 0;
}

static int sensor_device_pick_pending_event_locked(SensorDevice* dev,
                                                   sensors_event_t*  event)
{
    uint32_t mask = dev->pending_sensors;
    if (mask) {
        uint32_t i = 31 - __builtin_clz(mask);
        dev->pending_sensors &= ~(1U << i);
        *event = dev->sensors[i];
        if (dev->sensors[i].type == SENSOR_TYPE_META_DATA) {
            if (dev->flush_count[i] > 0) {
                (dev->flush_count[i])--;
                dev->pending_sensors |= (1U << i);
            } 
            else {
                dev->sensors[i].type = SENSOR_TYPE_META_DATA + 1;
            }
        } else {
            event->sensor = i;
            event->version = sizeof(*event);
        }

        return i;
    }
    ALOGD("no sensor to return!!! pending_sensors=0x%08x", dev->pending_sensors);
    // we may end-up in a busy loop, slow things down, just in case.
    usleep(1000);
    return -EINVAL;
}

static int sensor_device_poll(struct sensors_poll_device_t *dev0,
                              sensors_event_t* data, int count)
{
    SensorDevice* dev = (void*)dev0;
    if (count <= 0) {
        return -EINVAL;
    }
    int result = 0;
    pthread_mutex_lock(&dev->lock);
    if (!dev->pending_sensors) {
        /* Block until there are pending events. Note that this releases
         * the lock during the blocking call, then re-acquires it before
         * returning. */
        int ret = sensor_device_poll_event_locked(dev);
        if (ret < 0) {
            result = ret;
            goto out;
        }
    }
    /* Now read as many pending events as needed. */
    for (int i = 0; i < count; i++)  {
        if (!dev->pending_sensors) {
            break;
        }
        int ret = sensor_device_pick_pending_event_locked(dev, data);
        if (ret < 0) {
            if (!result) {
                result = ret;
            }
            break;
        }
        data++;
        result++;
    }
out:
    pthread_mutex_unlock(&dev->lock);
    return result;
}

static int sensor_device_activate(struct sensors_poll_device_t *dev0, int handle, int enabled) {
    SensorDevice* dev = (void*)dev0;
    int id = get_type_from_hanle(handle);
    if(id < 0){
        ALOGE("unknown handle(%d)", handle);
        return -EINVAL;
    }

    sensor_config_msg_t sensor_config_msg;
    memset(&sensor_config_msg, 0, sizeof(sensor_config_msg_t));
    sensor_config_msg.cmd_type = CMD_SENSOR_ACTIVATE;
    sensor_config_msg.enabled = enabled;
    sensor_config_msg.sensor_type = id;

    ALOGI("activate: sensor type=%d, handle=%s(%d), enabled=%d", sensor_config_msg.sensor_type, get_name_from_handle(handle), handle, sensor_config_msg.enabled);
    pthread_mutex_lock(&dev->lock);
    int ret = sensor_device_send_config_msg(dev, &sensor_config_msg, sizeof(sensor_config_msg_t));
    pthread_mutex_unlock(&dev->lock);
    if (ret < 0) {
        ALOGE("could not send activate command: %s", strerror(-ret));
        return -errno;
    }
    return 0;
}

static int sensor_device_flush(struct sensors_poll_device_1* dev0, int handle) {
    SensorDevice* dev = (void*)dev0;
    pthread_mutex_lock(&dev->lock);
    if ((dev->pending_sensors & (1U << handle)) && dev->sensors[handle].type == SENSOR_TYPE_META_DATA) {
        (dev->flush_count[handle])++;
    } else {
        dev->flush_count[handle] = 0;
        dev->sensors[handle].version = META_DATA_VERSION;
        dev->sensors[handle].type = SENSOR_TYPE_META_DATA;
        dev->sensors[handle].sensor = 0;
        dev->sensors[handle].timestamp = 0;
        dev->sensors[handle].meta_data.sensor = handle;
        dev->sensors[handle].meta_data.what = META_DATA_FLUSH_COMPLETE;
        dev->pending_sensors |= (1U << handle);
    }
    pthread_mutex_unlock(&dev->lock);
    return 0;
}

static int sensor_device_set_delay(struct sensors_poll_device_t *dev0, int handle __unused, int64_t ns)
{
    SensorDevice* dev = (void*)dev0;

    sensor_config_msg_t sensor_config_msg;
    memset(&sensor_config_msg, 0, sizeof(sensor_config_msg_t));
    int id = get_type_from_hanle(handle);
    if(id < 0){
        ALOGE("unknown handle (%d)", handle);
        return -EINVAL;
    }

    sensor_config_msg.cmd_type = CMD_SENSOR_BATCH;
    sensor_config_msg.sensor_type = id;
    sensor_config_msg.sample_period = (int32_t)(ns/1000000);
    ALOGD("set_delay: sensor type=%d, handle=%s(%d), sample_period=%dms", sensor_config_msg.sensor_type, get_name_from_handle(handle), handle, sensor_config_msg.sample_period);
    pthread_mutex_lock(&dev->lock);
    int ret = sensor_device_send_config_msg(dev, &sensor_config_msg, sizeof(sensor_config_msg)); 
    pthread_mutex_unlock(&dev->lock);
    if (ret < 0) {
        ALOGE("could not send batch command: %s", strerror(-ret));
        return -EINVAL;
    }
    return 0;
}

static int sensor_device_batch(
    struct sensors_poll_device_1* dev,
    int sensor_handle,
    int flags,
    int64_t sampling_period_ns,
    int64_t max_report_latency_ns) {
    SensorDevice* dev0 = (void*)dev;
    int ms = (int)(sampling_period_ns/1000000);
    int id = get_type_from_hanle(sensor_handle);
    if(id < 0){
        ALOGE("unknown handle (%d)", sensor_handle);
        return -EINVAL;
    }
    sensor_config_msg_t sensor_config_msg;
    memset(&sensor_config_msg, 0, sizeof(sensor_config_msg_t));
    sensor_config_msg.cmd_type = CMD_SENSOR_BATCH;
    sensor_config_msg.sensor_type = id;
    sensor_config_msg.sample_period = (int32_t)(sampling_period_ns/1000000);

    sensor_device_activate((struct sensors_poll_device_t *)dev, sensor_handle, 1);  //before batch, make sure the sensor have been enabled

    pthread_mutex_lock(&dev0->lock);  
    ALOGD("batch: sensor type=%d, handle=%s(%d), sample_period=%dms", sensor_config_msg.sensor_type, get_name_from_handle(sensor_handle), sensor_handle, sensor_config_msg.sample_period);
    int ret = sensor_device_send_config_msg(dev0, &sensor_config_msg, sizeof(sensor_config_msg)); 
    pthread_mutex_unlock(&dev0->lock);

    if (ret < 0) {
        ALOGE("could not send batch command: %s", strerror(-ret));
        return -errno;
    }
    return 0;
}

/** MODULE REGISTRATION SUPPORT
 **
 ** This is required so that hardware/libhardware/hardware.c
 ** will dlopen() this library appropriately.
 **/

/*
 * the following is the list of all supported sensors.
 * this table is used to build sSensorList declared below
 * according to which hardware sensors are reported as
 * available from the emulator (see get_sensors_list below)
 *
 * note: numerical values for maxRange/resolution/power for
 *       all sensors but light, pressure and humidity were
 *       taken from the reference AK8976A implementation
 */
static const struct sensor_t sSensorListInit[] = {
        { .name       = "AIC 3-axis Accelerometer",
          .vendor     = "Intel ACGSS",
          .version    = 1,
          .handle     = ID_ACCELERATION,
          .type       = SENSOR_TYPE_ACCELEROMETER,
          .maxRange   = 2.8f,
          .resolution = 1.0f/4032.0f,
          .power      = 3.0f,
          .minDelay   = 10000,
          .maxDelay   = 500 * 1000,
          .fifoReservedEventCount = 0,
          .fifoMaxEventCount =   0,
          .stringType = "android.sensor.accelerometer",
          .requiredPermission = 0,
          .flags = SENSOR_FLAG_CONTINUOUS_MODE,
          .reserved   = {}
        },

        { .name       = "AIC 3-axis Gyroscope",
          .vendor     = "Intel ACGSS",
          .version    = 1,
          .handle     = ID_GYROSCOPE,
          .type       = SENSOR_TYPE_GYROSCOPE,
          .maxRange   = 11.1111111,
          .resolution = 1.0f/1000.0f,
          .power      = 3.0f,
          .minDelay   = 10000,
          .maxDelay   = 500 * 1000,
          .stringType = "android.sensor.gyroscope",
          .reserved   = {}
        },

        { .name       = "AIC 3-axis Magnetic field sensor",
          .vendor     = "Intel ACGSS",
          .version    = 1,
          .handle     = ID_MAGNETIC_FIELD,
          .type       = SENSOR_TYPE_MAGNETIC_FIELD,
          .maxRange   = 2000.0f,
          .resolution = 1.0f,
          .power      = 6.7f,
          .minDelay   = 10000,
          .maxDelay   = 500 * 1000,
          .fifoReservedEventCount = 0,
          .fifoMaxEventCount =   0,
          .stringType = "android.sensor.magnetic_field",
          .requiredPermission = 0,
          .flags = SENSOR_FLAG_CONTINUOUS_MODE,
          .reserved   = {}
        },
};

static struct sensor_t sSensorList[MAX_NUM_SENSORS];

static int sensors__get_sensors_list(struct sensors_module_t* module __unused,
        struct sensor_t const** list)
{
    int client_fd = create_server_socket();
    if(client_fd < 0 ) {
        ALOGE("%s: no socket connection", __FUNCTION__);
        return -1;
    }
    *list = sSensorListInit;
    ALOGD("get sensor list, support %d sensors", MAX_NUM_SENSORS);
    return MAX_NUM_SENSORS;
}


static int open_sensors(const struct hw_module_t* module, const char* name, struct hw_device_t* *device)
{
    int  status = -EINVAL;
    ALOGD("open_sensors");
    if (!strcmp(name, SENSORS_HARDWARE_POLL)) {
        SensorDevice *dev = malloc(sizeof(*dev));
        memset(dev, 0, sizeof(*dev));

        dev->device.common.tag     = HARDWARE_DEVICE_TAG;
        dev->device.common.version = SENSORS_DEVICE_API_VERSION_1_3;
        dev->device.common.module  = (struct hw_module_t*) module;
        dev->device.common.close   = sensor_device_close;
        dev->device.poll           = sensor_device_poll;
        dev->device.activate       = sensor_device_activate;
        dev->device.setDelay       = sensor_device_set_delay;

        // (dev->sensors[i].type == SENSOR_TYPE_META_DATA) is
        // sticky. Don't start off with that setting.
        for (int idx = 0; idx < MAX_NUM_SENSORS; idx++) {
            dev->sensors[idx].type = SENSOR_TYPE_META_DATA + 1;
            dev->flush_count[idx] = 0;
        }

// Version 1.3-specific functions
#if defined(SENSORS_DEVICE_API_VERSION_1_3)
        dev->device.batch       = sensor_device_batch;
        dev->device.flush       = sensor_device_flush;
#endif
        pthread_mutex_init(&dev->lock, NULL);

        int fd = sensor_device_get_socket_fd();

        if(fd < 0){
            dev->fd = -1;
            ALOGE("invalid socket fd: %s", __FUNCTION__);
        }else{
            dev->fd = fd;
        }
        *device = &dev->device.common;
        status  = 0;
    };
    return status;
}


static struct hw_module_methods_t sensors_module_methods = {
    .open = open_sensors
};

struct sensors_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .version_major = 1,
        .version_minor = 3,
        .id = SENSORS_HARDWARE_MODULE_ID,
        .name = "AIC SENSORS Module",
        .author = "Intel ACGSS",
        .methods = &sensors_module_methods,
    },
    .get_sensors_list = sensors__get_sensors_list
};
