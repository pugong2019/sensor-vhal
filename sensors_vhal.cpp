/*
** Copyright 2018 Intel Corporation
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/
 
#include "sensors_vhal.h"
#include "sock_utils.h"

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
    int                           flush_count[MAX_NUM_SENSORS];
    pthread_mutex_t               lock;
    SockServer*                   socket_server;
} SensorDevice;

void client_connected_callback(SockServer* sock, sock_client_proxy_t* client){
    ALOGE("sensors client connected successfully");
}

static const char* get_name_from_handle( int  id ) {
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

/* Send a command to the sensors virtual device. |dev| is a device instance and
 * |cmd| is a zero-terminated command string. Return 0 on success, or -errno
 * on failure. */
static int sensor_device_send_config_msg(SensorDevice* dev, const void* cmd, size_t len) {
    sock_client_proxy_t* client = dev->socket_server->get_sock_client();
    if(!client){
        // ALOGW("client has not connected to sensors vhal server, do nothing"); //ignore it if the client has not connected to server
        return 0; // set 0 as success. or SensorService may crash
    }
    int ret = dev->socket_server->send_data(client, cmd, len);
    if (ret < 0) {
        ret = -errno;
        ALOGE("%s: ERROR: %s", __FUNCTION__, strerror(errno));
    }
    return ret;
}

static int sensor_device_close(struct hw_device_t* dev0)
{
    ALOGE("close sensor device");
    SensorDevice* dev = (SensorDevice*)dev0;
    dev->socket_server->stop();
    dev->socket_server->join();
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

/* Return an array of sensor data. This function blocks until there are sensor
 * related events to report. On success, it will write the events into the
 * |data| array, which contains |count| items. The function returns the number
 * of events written into the array, which shall never be greater than |count|.
 * On error, return -errno code.
 *
 * Note that according to the sensor HAL [1], it shall never return 0!
 */

static int sensor_device_poll_event_locked(SensorDevice* dev){
#if DEBUG_OPTION
    static double last_acc_time = 0;
    static double last_gyro_time = 0;
    static double last_mag_time = 0;
    static int64_t acc_count = 0;
    static int64_t gyr_count = 0;
    static int64_t mag_count = 0;
#endif

    acgmsg_sensors_event_t new_sensor_events;
    sensors_event_t* events = dev->sensors;
    int len = -1;
    uint32_t new_sensors = 0U;
    sock_client_proxy_t* client = dev->socket_server->get_sock_client();

    for(;;){ // make sure recv one event
        if(!client){
            pthread_mutex_unlock(&dev->lock);
            usleep(2*1000); //sleep and wait the client connected to server, and release the lock
            pthread_mutex_lock(&dev->lock);
            client = dev->socket_server->get_sock_client(); //require the lock
            continue;
        }
        pthread_mutex_unlock(&dev->lock);
        len = dev->socket_server->recv_data(client, &new_sensor_events, sizeof(acgmsg_sensors_event_t), SOCK_BLOCK_MODE);
        pthread_mutex_lock(&dev->lock);

        if (len < 0) {
            ALOGE("sensors vhal receive data failed: %s ", strerror(errno));
            return -errno;
        }
        switch (new_sensor_events.type)
        {
            case SENSOR_TYPE_ACCELEROMETER:
                new_sensors |= SENSORS_ACCELERATION;
                events[ID_ACCELERATION].acceleration.x = new_sensor_events.acceleration.x;
                events[ID_ACCELERATION].acceleration.y = new_sensor_events.acceleration.y;
                events[ID_ACCELERATION].acceleration.z = new_sensor_events.acceleration.z;
                events[ID_ACCELERATION].timestamp = new_sensor_events.timestamp;
                events[ID_ACCELERATION].type = SENSOR_TYPE_ACCELEROMETER;

#if DEBUG_OPTION
                acc_count++;
                if(acc_count%100 == 0){
                    ALOGD("[%-5d] Acc: %f,%f,%f, time = %.3fms", acc_count, new_sensor_events.acceleration.x, new_sensor_events.acceleration.y, new_sensor_events.acceleration.z, ((double)(new_sensor_events.timestamp-last_acc_time))/1000000.0);
                }
                last_acc_time = new_sensor_events.timestamp;
#endif
                break;

            case SENSOR_TYPE_GYROSCOPE:
                new_sensors |= SENSORS_GYROSCOPE;
                events[ID_GYROSCOPE].gyro.x = new_sensor_events.gyro.x;
                events[ID_GYROSCOPE].gyro.y = new_sensor_events.gyro.y;
                events[ID_GYROSCOPE].gyro.z = new_sensor_events.gyro.z;
                events[ID_GYROSCOPE].timestamp = new_sensor_events.timestamp;
                events[ID_ACCELERATION].type = SENSOR_TYPE_GYROSCOPE;

#if DEBUG_OPTION
                gyr_count++;
                if(gyr_count%100 == 0){
                    ALOGD("[%-5d] Gyr: %f,%f,%f, time = %.3fms", gyr_count, new_sensor_events.acceleration.x, new_sensor_events.acceleration.y, new_sensor_events.acceleration.z, ((double)(new_sensor_events.timestamp-last_gyro_time))/1000000.0);
                }
                last_gyro_time = new_sensor_events.timestamp;
#endif
                break;

            case SENSOR_TYPE_MAGNETIC_FIELD:
                new_sensors |= SENSORS_MAGNETIC_FIELD;
                events[ID_MAGNETIC_FIELD].magnetic.x = new_sensor_events.magnetic.x;
                events[ID_MAGNETIC_FIELD].magnetic.y = new_sensor_events.magnetic.y;
                events[ID_MAGNETIC_FIELD].magnetic.z = new_sensor_events.magnetic.z;
                events[ID_MAGNETIC_FIELD].timestamp = new_sensor_events.timestamp;
                events[ID_ACCELERATION].type = SENSOR_TYPE_MAGNETIC_FIELD;

#if DEBUG_OPTION
                mag_count++;
                if(mag_count%100 == 0){
                    ALOGD("[%-5d] Mag: %f,%f,%f, time = %.3fms", mag_count, new_sensor_events.acceleration.x, new_sensor_events.acceleration.y, new_sensor_events.acceleration.z, ((double)(new_sensor_events.timestamp-last_mag_time))/1000000.0);
                }
                last_mag_time = new_sensor_events.timestamp;
#endif
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
    uint32_t mask = SUPPORTED_SENSORS & dev->pending_sensors;
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
    SensorDevice* dev = (SensorDevice*)dev0;
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
    SensorDevice* dev = (SensorDevice*)dev0;
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
    SensorDevice* dev = (SensorDevice*)dev0;
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
    SensorDevice* dev = (SensorDevice*)dev0;

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
    int flags __unused,
    int64_t sampling_period_ns,
    int64_t max_report_latency_ns __unused) {

    SensorDevice* dev0 = (SensorDevice*)dev;
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

static int sensors__get_sensors_list(struct sensors_module_t* module __unused,
        struct sensor_t const** list)
{
    *list = sSensorListInit;
    ALOGD("get sensor list, support %d sensors", MAX_NUM_SENSORS);
    return MAX_NUM_SENSORS;
}


static int open_sensors(const struct hw_module_t* module, const char* name, struct hw_device_t* *device)
{
    int  status = -EINVAL;
    ALOGD("open_sensors");
    if (!strcmp(name, SENSORS_HARDWARE_POLL)) {
        SensorDevice *dev = (SensorDevice*)malloc(sizeof(*dev));
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

        // int fd = sensor_device_get_socket_fd();
        char buf[PROPERTY_VALUE_MAX] = {'\0',};
        int virtual_sensor_port = SENSOR_VHAL_PORT;
        if (property_get(SENSOR_VHAL_PORT_PROP, buf, NULL) > 0) {
            virtual_sensor_port = atoi(buf);
        }
        dev->socket_server = new SockServer(virtual_sensor_port);
        dev->socket_server->register_connected_callback(client_connected_callback);
        dev->socket_server->start();

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
