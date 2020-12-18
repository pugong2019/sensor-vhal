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
#define MAX_MSG_QUEUE_SIZE    128

typedef struct {
    int32_t    cmd_type;         // acgmsg_sensor_conig_type_t
    int32_t    sensor_type;       // acgmsg_sensor_type_t
    union {
       int32_t    enabled;       // acgmsg_sensor_status_t for cmd: ACG_SENSOR_ACTIVATE
       int32_t    sample_period; // ACG_SENSOR_BATCH
    };
} sensor_config_msg_t;

template <typename T, typename... Args>
std::unique_ptr<T> make_unique_cg(Args &&... args) {
  return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

class SensorDevice {
public:
    struct sensors_poll_device_1  device; //must be first
    SensorDevice();
    ~SensorDevice();
    int sensor_device_poll(sensors_event_t* data, int count);
    int sensor_device_activate(int handle, int enabled);
    int sensor_device_flush(int handle);
    int sensor_device_set_delay(int handle, int64_t ns);
    int sensor_device_batch(int sensor_handle, int64_t sampling_period_ns);

private:
    sensors_event_t               sensors[MAX_NUM_SENSORS];
    int                           flush_count[MAX_NUM_SENSORS];
    uint32_t                      pending_sensors;
    int64_t                       time_start;
    int64_t                       time_offset;
    pthread_mutex_t               lock;
    SockServer*                   socket_server;
    std::mutex                    m_msg_queue_mutex;
    std::condition_variable       m_msg_queue_ready_cv;
    std::condition_variable       m_msg_queue_empty_cv;
    std::queue<acgmsg_sensors_event_t> m_msg_queue;

private:
    int64_t now_ns(void);
    const char* get_name_from_handle(int id );
    int get_type_from_hanle(int handle);
    int sensor_device_poll_event_locked();
    int sensor_device_send_config_msg(const void* cmd, size_t len);
    int sensor_device_pick_pending_event_locked(sensors_event_t*  event);
    void sensor_event_callback(SockServer *sock, sock_client_proxy_t* client);
};

SensorDevice::SensorDevice(){
    pthread_mutex_init(&lock, NULL);
    ALOGE("new sensor device");
    for (int idx = 0; idx < MAX_NUM_SENSORS; idx++) {
        sensors[idx].type = SENSOR_TYPE_META_DATA + 1;
        flush_count[idx] = 0;
    }
    char buf[PROPERTY_VALUE_MAX] = {'\0',};
    int virtual_sensor_port = SENSOR_VHAL_PORT;
    if (property_get(SENSOR_VHAL_PORT_PROP, buf, NULL) > 0) {
        virtual_sensor_port = atoi(buf);
    }
    socket_server = new SockServer(virtual_sensor_port);
    // socket_server->register_connected_callback(client_connected_callback);
    socket_server->start();
}

SensorDevice::~SensorDevice(){
    pthread_mutex_destroy(&lock);
    delete socket_server;
}

void client_connected_callback(SockServer* sock, sock_client_proxy_t* client){
    ALOGE("sensors client connected successfully");
}

const char* SensorDevice::get_name_from_handle( int  id ) {
    int  nn;
    for (nn = 0; nn < MAX_NUM_SENSORS; nn++)
        if (id == _sensorIds[nn].id)
            return _sensorIds[nn].name;
    return "<UNKNOWN>";
}

/* return the current time in nanoseconds */
int64_t SensorDevice::now_ns(void) {
    struct timespec  ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

int SensorDevice::sensor_device_send_config_msg(const void* cmd, size_t len) {
    sock_client_proxy_t* client = socket_server->get_sock_client();
    if(!client){
        return 0; // set 0 as success. or SensorService may crash
    }
    int ret = socket_server->send_data(client, cmd, len);
    if (ret < 0) {
        ret = -errno;
        ALOGE("%s: ERROR: %s", __FUNCTION__, strerror(errno));
    }
    return ret;
}

static int sensor_close(struct hw_device_t* dev0){
    ALOGE("close sensor device");
    SensorDevice* dev = (SensorDevice* )dev0;
    delete dev;
    dev = nullptr;
    return 0;
}

int SensorDevice::get_type_from_hanle(int handle){
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

int SensorDevice::sensor_device_poll_event_locked(){
#if DEBUG_OPTION
    static double last_acc_time = 0;
    static double last_gyro_time = 0;
    static double last_mag_time = 0;
    static int64_t acc_count = 0;
    static int64_t gyr_count = 0;
    static int64_t mag_count = 0;
#endif

    acgmsg_sensors_event_t new_sensor_events;
    sensors_event_t* events = sensors;
    int len = -1;
    uint32_t new_sensors = 0U;

 // make sure recv one event
    for(;;){
        sock_client_proxy_t* client = socket_server->get_sock_client();
        if(!client){
            pthread_mutex_unlock(&lock);
            // ALOGE("client has not connected yet, wait 2ms");
            usleep(2*1000);   //sleep and wait the client connected to server, and release the lock before sleep
            pthread_mutex_lock(&lock);
            continue;
        }
        pthread_mutex_unlock(&lock);
        len = socket_server->recv_data(client, &new_sensor_events, sizeof(acgmsg_sensors_event_t), SOCK_BLOCK_MODE);
        pthread_mutex_lock(&lock);

        if (len <= 0) {
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
        pending_sensors |= new_sensors;
        int64_t remote_timestamp = new_sensor_events.timestamp;
        int64_t host_timestamp = now_ns();
        if (time_start == 0) {
            time_start  = host_timestamp;
            time_offset = time_start - remote_timestamp;
        }

        remote_timestamp += time_offset;
        if (remote_timestamp > host_timestamp) {
            remote_timestamp = host_timestamp;
        }
        while (new_sensors) {
            uint32_t i = 31 - __builtin_clz(new_sensors);
            new_sensors &= ~(1U << i);
            sensors[i].timestamp = remote_timestamp;
        }
    }
    return 0;
}

int SensorDevice::sensor_device_pick_pending_event_locked(sensors_event_t*  event){
    uint32_t mask = SUPPORTED_SENSORS & pending_sensors;
    if (mask) {
        uint32_t i = 31 - __builtin_clz(mask);
        pending_sensors &= ~(1U << i);
        *event = sensors[i];
        if (sensors[i].type == SENSOR_TYPE_META_DATA) {
            if (flush_count[i] > 0) {
                (flush_count[i])--;
                pending_sensors |= (1U << i);
            } 
            else {
                sensors[i].type = SENSOR_TYPE_META_DATA + 1;
            }
        } else {
            event->sensor = i;
            event->version = sizeof(*event);
        }

        return i;
    }
    ALOGD("no sensor to return!!! pending_sensors=0x%08x", pending_sensors);
    // we may end-up in a busy loop, slow things down, just in case.
    usleep(1000);
    return -EINVAL;
}

static int sensor_poll_events(struct sensors_poll_device_t *dev0, sensors_event_t* data, int count) {
    if (count <= 0) {
        return -EINVAL;
    }
    SensorDevice *dev = (SensorDevice* ) dev0;
    return dev->sensor_device_poll(data, count);
}

int SensorDevice::sensor_device_poll(sensors_event_t* data, int count) {
    int result = 0;
    pthread_mutex_lock(&lock);
    if (!pending_sensors) {
        /* Block until there are pending events. Note that this releases
         * the lock during the blocking call, then re-acquires it before
         * returning. */
        int ret = sensor_device_poll_event_locked();
        if (ret < 0) {
            result = ret;
            goto out;
        }
    }
    /* Now read as many pending events as needed. */
    for (int i = 0; i < count; i++) {
        if (!pending_sensors) {
            break;
        }
        int ret = sensor_device_pick_pending_event_locked(data);
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
    pthread_mutex_unlock(&lock);
    return result;
}

static int sensor_activate(struct sensors_poll_device_t *dev0, int handle, int enabled){
    SensorDevice* dev = (SensorDevice*) dev0;
    return dev->sensor_device_activate(handle, enabled);
}

int SensorDevice::sensor_device_activate(int handle, int enabled) {
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
    pthread_mutex_lock(&lock);
    int ret = sensor_device_send_config_msg(&sensor_config_msg, sizeof(sensor_config_msg_t));
    pthread_mutex_unlock(&lock);
    if (ret < 0) {
        ALOGE("could not send activate command: %s", strerror(-ret));
        return -errno;
    }
    return 0;
}

static int sensor_flush(struct sensors_poll_device_1* dev0, int handle) { 
    SensorDevice* dev = (SensorDevice*)dev0;
    return dev->sensor_device_flush(handle);
}

int SensorDevice::sensor_device_flush(int handle) {
    pthread_mutex_lock(&lock);
    if ((pending_sensors & (1U << handle)) && sensors[handle].type == SENSOR_TYPE_META_DATA) {
        (flush_count[handle])++;
    } else {
        flush_count[handle] = 0;
        sensors[handle].version = META_DATA_VERSION;
        sensors[handle].type = SENSOR_TYPE_META_DATA;
        sensors[handle].sensor = 0;
        sensors[handle].timestamp = 0;
        sensors[handle].meta_data.sensor = handle;
        sensors[handle].meta_data.what = META_DATA_FLUSH_COMPLETE;
        pending_sensors |= (1U << handle);
    }
    pthread_mutex_unlock(&lock);
    return 0;
}

static int sensor_set_delay(struct sensors_poll_device_t *dev0, int handle __unused, int64_t ns) {
    SensorDevice* dev = (SensorDevice*)dev0;
    return dev->sensor_device_set_delay(handle, ns);
}

int SensorDevice::sensor_device_set_delay(int handle, int64_t ns) {
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
    pthread_mutex_lock(&lock);
    int ret = sensor_device_send_config_msg(&sensor_config_msg, sizeof(sensor_config_msg)); 
    pthread_mutex_unlock(&lock);
    if (ret < 0) {
        ALOGE("could not send batch command: %s", strerror(-ret));
        return -EINVAL;
    }
    return 0;
}

static int sensor_batch(struct sensors_poll_device_1* dev0,
    int sensor_handle,
    int flags __unused,
    int64_t sampling_period_ns,
    int64_t max_report_latency_ns __unused) {

    SensorDevice* dev = (SensorDevice*) dev0;
    return dev->sensor_device_batch(sensor_handle, sampling_period_ns);
}

int SensorDevice::sensor_device_batch(
    int sensor_handle,
    int64_t sampling_period_ns) {

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

    sensor_device_activate(sensor_handle, 1);  //before batch, make sure the sensor have been enabled

    pthread_mutex_lock(&lock);
    ALOGD("batch: sensor type=%d, handle=%s(%d), sample_period=%dms", sensor_config_msg.sensor_type, get_name_from_handle(sensor_handle), sensor_handle, sensor_config_msg.sample_period);
    int ret = sensor_device_send_config_msg(&sensor_config_msg, sizeof(sensor_config_msg)); 
    pthread_mutex_unlock(&lock);

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


static int open_sensors(const struct hw_module_t* module, const char* name, struct hw_device_t** device)
{
    int  status = -EINVAL;
    ALOGD("open_sensors");
    if (!strcmp(name, SENSORS_HARDWARE_POLL)) {
        SensorDevice *dev = new SensorDevice();

        // memset(dev, 0, sizeof(*dev));
        dev->device.common.tag     = HARDWARE_DEVICE_TAG;
        dev->device.common.version = SENSORS_DEVICE_API_VERSION_1_3;
        dev->device.common.module  = (struct hw_module_t*) module;
        dev->device.common.close   = sensor_close;
        dev->device.poll           = sensor_poll_events;
        dev->device.activate       = sensor_activate;
        dev->device.setDelay       = sensor_set_delay;

        // (dev->sensors[i].type == SENSOR_TYPE_META_DATA) is
        // sticky. Don't start off with that setting.
// Version 1.3-specific functions
#if defined(SENSORS_DEVICE_API_VERSION_1_3)
        dev->device.batch       = sensor_batch;
        dev->device.flush       = sensor_flush;
#endif
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


void SensorDevice::sensor_event_callback(SockServer *sock, sock_client_proxy_t* client){
    acgmsg_sensors_event_t new_sensor_events;

    pthread_mutex_unlock(&lock);
    int len = socket_server->recv_data(client, &new_sensor_events, sizeof(acgmsg_sensors_event_t), SOCK_BLOCK_MODE);
    pthread_mutex_lock(&lock);

    if (len <= 0) {
        ALOGE("sensors vhal receive data failed: %s ", strerror(errno));
       return;
    }
   
    std::unique_lock<std::mutex> lock(m_msg_queue_mutex);
    while(m_msg_queue.size() > MAX_MSG_QUEUE_SIZE){
        ALOGW("the msg queue is too large: %d, waiting proccessed...", (int)m_msg_queue.size());
        m_msg_queue_empty_cv.wait(lock);
    }

    // std::unique_ptr<acgmsg_sensors_event_t> msg = make_unique_cg<acgmsg_sensors_event_t>(&new_sensor_events);
    m_msg_queue.emplace(std::move(new_sensor_events));
    m_msg_queue_ready_cv.notify_all();   
}
