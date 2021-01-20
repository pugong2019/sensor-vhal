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

using namespace ::std::placeholders;

template <typename T, typename... Ts>
std::unique_ptr<T> make_unique_ptr(Ts&&... params) {
    return std::unique_ptr<T>(new T(std::forward<Ts>(params)...));
}

SensorDevice::SensorDevice() {
    for (int idx = 0; idx < MAX_NUM_SENSORS; idx++) {
        m_sensors[idx].type = SENSOR_TYPE_META_DATA + 1;
        m_flush_count[idx]  = 0;
        memset(&m_sensor_config_status[idx], 0, sizeof(sensor_config_msg_t));
    }
    char buf[PROPERTY_VALUE_MAX] = {
        '\0',
    };
    int virtual_sensor_port = SENSOR_VHAL_PORT;
    if (property_get(SENSOR_VHAL_PORT_PROP, buf, NULL) > 0) {
        virtual_sensor_port = atoi(buf);
    }
    int buf_size = sizeof(aic_sensors_event_t) + MAX_SENSOR_PAYLOAD_SIZE;

    for (int i = 0; i < MEM_POOL_SIZE; i++) {
        m_msg_mem_pool.emplace(make_unique_ptr<std::vector<char>>(buf_size));
    }

    m_socket_server = new SockServer(virtual_sensor_port);
    m_socket_server->register_listener_callback(std::bind(&SensorDevice::sensor_event_callback, this, _1, _2));
    m_socket_server->register_connected_callback(std::bind(&SensorDevice::client_connected_callback, this, _1, _2));
    m_socket_server->start();
}

SensorDevice::~SensorDevice() {
    delete m_socket_server;
    m_socket_server = nullptr;
}

const char* SensorDevice::get_name_from_handle(int id) {
    int nn;
    for (nn = 0; nn < MAX_NUM_SENSORS; nn++)
        if (id == _sensorIds[nn].id) return _sensorIds[nn].name;
    return "<UNKNOWN>";
}

/* return the current time in nanoseconds */
int64_t SensorDevice::now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

int SensorDevice::sensor_device_send_config_msg(const void* cmd, size_t len) {
    sock_client_proxy_t* client = m_socket_server->get_sock_client();
    if (!client) {
        return 0;  // set 0 as success. or SensorService may crash
    }
    int ret = m_socket_server->send_data(client, cmd, len);
    if (ret < 0) {
        ret = -errno;
        ALOGE("%s: ERROR: %s", __FUNCTION__, strerror(errno));
    }
    return ret;
}

int SensorDevice::get_type_from_hanle(int handle) {
    int id = -1;
    switch (handle) {
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
            ALOGW("unknown handle (%d)", handle);
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

int SensorDevice::sensor_device_poll_event_locked() {
#if DEBUG_OPTION
    static double last_acc_time  = 0;
    static double last_gyro_time = 0;
    static double last_mag_time  = 0;
    static int acc_count         = 0;
    static int gyr_count         = 0;
    static int mag_count         = 0;
#endif

    aic_sensors_event_t* new_sensor_events_ptr = nullptr;
    std::unique_ptr<std::vector<char>> buf_ptr;
    sensors_event_t* events = m_sensors;
    uint32_t new_sensors    = 0U;
    // make sure recv one event
    for (;;) {
        sock_client_proxy_t* client = m_socket_server->get_sock_client();
        if (!client) {
            m_mutex.unlock();
            usleep(2 * 1000);  // sleep and wait the client connected to server, and release the lock before sleep
            m_mutex.lock();
            continue;
        }

        m_mutex.unlock();  // waitging for sensor message
        {
            std::unique_lock<std::mutex> lock(m_msg_queue_mtx);
            if (m_sensor_msg_queue.empty()) {
                m_msg_queue_ready_cv.wait(lock);
            }
            buf_ptr = std::move(m_sensor_msg_queue.front());
            m_sensor_msg_queue.pop();
        }

        m_mutex.lock();
        if (buf_ptr && buf_ptr->empty()) {
            continue;
        }
        new_sensor_events_ptr = (aic_sensors_event_t*)buf_ptr->data();
        sensors_event_t* events = m_sensors;
        switch (new_sensor_events_ptr->type) {
            case SENSOR_TYPE_ACCELEROMETER:
                new_sensors |= SENSORS_ACCELERATION;
                events[ID_ACCELERATION].acceleration.x = new_sensor_events_ptr->data.fdata[0];
                events[ID_ACCELERATION].acceleration.y = new_sensor_events_ptr->data.fdata[1];
                events[ID_ACCELERATION].acceleration.z = new_sensor_events_ptr->data.fdata[2];
                events[ID_ACCELERATION].timestamp      = new_sensor_events_ptr->timestamp;
                events[ID_ACCELERATION].type           = SENSOR_TYPE_ACCELEROMETER;

#if DEBUG_OPTION
                acc_count++;
                if (acc_count % 100 == 0) {
                    ALOGD("[%-5d] Acc: %f,%f,%f, time = %.3fms", acc_count, new_sensor_events_ptr->data.fdata[0], new_sensor_events_ptr->data.fdata[1],
                          new_sensor_events_ptr->data.fdata[2], ((double)(new_sensor_events_ptr->timestamp - last_acc_time)) / 1000000.0);
                }
                last_acc_time = new_sensor_events_ptr->timestamp;
#endif
                break;

            case SENSOR_TYPE_GYROSCOPE:
                new_sensors |= SENSORS_GYROSCOPE;
                events[ID_GYROSCOPE].gyro.x    = new_sensor_events_ptr->data.fdata[0];
                events[ID_GYROSCOPE].gyro.y    = new_sensor_events_ptr->data.fdata[1];
                events[ID_GYROSCOPE].gyro.z    = new_sensor_events_ptr->data.fdata[2];
                events[ID_GYROSCOPE].timestamp = new_sensor_events_ptr->timestamp;
                events[ID_ACCELERATION].type   = SENSOR_TYPE_GYROSCOPE;

#if DEBUG_OPTION
                gyr_count++;
                if (gyr_count % 100 == 0) {
                    ALOGD("[%-5d] Gyr: %f,%f,%f, time = %.3fms", gyr_count, new_sensor_events_ptr->data.fdata[0], new_sensor_events_ptr->data.fdata[1],
                          new_sensor_events_ptr->data.fdata[2], ((double)(new_sensor_events_ptr->timestamp - last_gyro_time)) / 1000000.0);
                }
                last_gyro_time = new_sensor_events_ptr->timestamp;
#endif
                break;

            case SENSOR_TYPE_MAGNETIC_FIELD:
                new_sensors |= SENSORS_MAGNETIC_FIELD;
                events[ID_MAGNETIC_FIELD].magnetic.x = new_sensor_events_ptr->data.fdata[0];
                events[ID_MAGNETIC_FIELD].magnetic.y = new_sensor_events_ptr->data.fdata[1];
                events[ID_MAGNETIC_FIELD].magnetic.z = new_sensor_events_ptr->data.fdata[2];
                events[ID_MAGNETIC_FIELD].timestamp  = new_sensor_events_ptr->timestamp;
                events[ID_ACCELERATION].type         = SENSOR_TYPE_MAGNETIC_FIELD;

#if DEBUG_OPTION
                mag_count++;
                if (mag_count % 100 == 0) {
                    ALOGD("[%-5d] Mag: %f,%f,%f, time = %.3fms", mag_count, new_sensor_events_ptr->data.fdata[0], new_sensor_events_ptr->data.fdata[1],
                          new_sensor_events_ptr->data.fdata[2], ((double)(new_sensor_events_ptr->timestamp - last_mag_time)) / 1000000.0);
                }
                last_mag_time = new_sensor_events_ptr->timestamp;
#endif
                break;

            default:
                ALOGW("unsupported sensor type: %d, continuing to receive next event", new_sensor_events_ptr->type);
                continue;
        }
        break;
    }

    /* update the time of each new sensor event. let's compare the remote
     * sensor timestamp with current time and take the lower value
     * --- we don't believe in events from the future anyway.
     */
    if (new_sensors) {
        m_pending_sensors |= new_sensors;
        int64_t remote_timestamp = new_sensor_events_ptr->timestamp;
        int64_t host_timestamp   = now_ns();
        if (m_time_start == 0) {
            m_time_start  = host_timestamp;
            m_time_offset = m_time_start - remote_timestamp;
        }

        remote_timestamp += m_time_offset;
        if (remote_timestamp > host_timestamp) {
            remote_timestamp = host_timestamp;
        }
        while (new_sensors) {
            uint32_t i = 31 - __builtin_clz(new_sensors);
            new_sensors &= ~(1U << i);
            events[i].timestamp = remote_timestamp;
        }
    }
    {
        std::unique_lock<std::mutex> lock(m_msg_pool_mtx);
        m_msg_mem_pool.emplace(std::move(buf_ptr));
    }
    return 0;
}

int SensorDevice::sensor_device_pick_pending_event_locked(sensors_event_t* event) {
    uint32_t mask = SUPPORTED_SENSORS & m_pending_sensors;
    if (mask) {
        uint32_t i = 31 - __builtin_clz(mask);
        m_pending_sensors &= ~(1U << i);
        *event = m_sensors[i];
        if (m_sensors[i].type == SENSOR_TYPE_META_DATA) {
            if (m_flush_count[i] > 0) {
                (m_flush_count[i])--;
                m_pending_sensors |= (1U << i);
            } else {
                m_sensors[i].type = SENSOR_TYPE_META_DATA + 1;
            }
        } else {
            event->sensor  = i;
            event->version = sizeof(*event);
        }

        return i;
    }
    ALOGW("no sensor to return!!! m_pending_sensors=0x%08x", m_pending_sensors);
    // we may end-up in a busy loop, slow things down, just in case.
    usleep(1000);
    return -EINVAL;
}

int SensorDevice::sensor_device_poll(sensors_event_t* data, int count) {
    int result = 0;
    m_mutex.lock();
    if (!m_pending_sensors) {
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
        if (!m_pending_sensors) {
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
    m_mutex.unlock();
    // ALOGW("sensor_device_poll  end result=%d", result);
    return result;
}

int SensorDevice::sensor_device_activate(int handle, int enabled) {
    int id = get_type_from_hanle(handle);
    if (id < 0) {
        ALOGE("unknown handle(%d)", handle);
        return -EINVAL;
    }

    m_mutex.lock();
    m_sensor_config_status[handle].sensor_type = id;
    m_sensor_config_status[handle].enabled     = enabled;
    ALOGI("activate: sensor type=%d, enabled=%d, handle=%s(%d)", id, enabled, get_name_from_handle(handle), handle);
    if (!enabled) {
        int ret = sensor_device_send_config_msg(&m_sensor_config_status[handle], sizeof(sensor_config_msg_t));
        if (ret < 0) {
            ALOGE("could not send activate command: %s", strerror(-ret));
            m_mutex.unlock();
            return -errno;
        }
    }
    m_mutex.unlock();
    return 0;
}

int SensorDevice::sensor_device_batch(int handle, int64_t sampling_period_ns) {
    int sensor_type = get_type_from_hanle(handle);
    if (sensor_type < 0) {
        ALOGE("unknown handle (%d)", handle);
        return -EINVAL;
    }
    int32_t sampling_period_ms = (int32_t)(sampling_period_ns / 1000000);

    m_mutex.lock();
    m_sensor_config_status[handle].sensor_type   = sensor_type;
    m_sensor_config_status[handle].enabled       = 1;
    m_sensor_config_status[handle].sample_period = sampling_period_ms;

    ALOGI("batch: sensor type=%d, sample_period=%dms, handle=%s(%d)", sensor_type, sampling_period_ms, get_name_from_handle(handle), handle);
    int ret = sensor_device_send_config_msg(&m_sensor_config_status[handle], sizeof(sensor_config_msg_t));
    m_mutex.unlock();

    if (ret < 0) {
        ALOGE("could not send batch command: %s", strerror(-ret));
        return -errno;
    }
    return 0;
}

int SensorDevice::sensor_device_set_delay(int handle, int64_t ns) {
    int sensor_type = get_type_from_hanle(handle);
    if (sensor_type < 0) {
        ALOGE("unknown handle (%d)", handle);
        return -EINVAL;
    }
    int32_t sampling_period_ms = (int32_t)(ns / 1000000);

    m_mutex.lock();
    m_sensor_config_status[handle].sensor_type   = sensor_type;
    m_sensor_config_status[handle].enabled       = 1;
    m_sensor_config_status[handle].sample_period = sampling_period_ms;

    ALOGI("set_delay: sensor type=%d, sample_period=%dms, handle=%s(%d)", sensor_type, sampling_period_ms, get_name_from_handle(handle), handle);
    int ret = sensor_device_send_config_msg(&m_sensor_config_status[handle], sizeof(sensor_config_msg_t));
    m_mutex.unlock();

    if (ret < 0) {
        ALOGE("could not send batch command: %s", strerror(-ret));
        return -errno;
    }
    return 0;
}

int SensorDevice::sensor_device_flush(int handle) {
    m_mutex.lock();
    if ((m_pending_sensors & (1U << handle)) && m_sensors[handle].type == SENSOR_TYPE_META_DATA) {
        (m_flush_count[handle])++;
    } else {
        m_flush_count[handle]              = 0;
        m_sensors[handle].version          = META_DATA_VERSION;
        m_sensors[handle].type             = SENSOR_TYPE_META_DATA;
        m_sensors[handle].sensor           = 0;
        m_sensors[handle].timestamp        = 0;
        m_sensors[handle].meta_data.sensor = handle;
        m_sensors[handle].meta_data.what   = META_DATA_FLUSH_COMPLETE;
        m_pending_sensors |= (1U << handle);
    }
    m_mutex.unlock();
    return 0;
}

int SensorDevice::get_index_from_type(int sensor_type) {
    int index = -1;
    switch (sensor_type) {
        case SENSOR_TYPE_ACCELEROMETER:
            index = ID_ACCELERATION;
            break;
        case SENSOR_TYPE_MAGNETIC_FIELD:
            index = ID_MAGNETIC_FIELD;
            break;
        case SENSOR_TYPE_GYROSCOPE:
            index = ID_GYROSCOPE;
            break;
        default:
            ALOGW("unsupported sensor type: %d", sensor_type);
            index = -1;
            break;
    }
    return index;
}

void SensorDevice::sensor_event_callback(SockServer* sock, sock_client_proxy_t* client) {
    aic_sensors_event_t sensor_events_header;
    int payload_len = 0;
    int len         = m_socket_server->recv_data(client, &sensor_events_header, sizeof(aic_sensors_event_t), SOCK_BLOCK_MODE);

    if (len <= 0) {
        ALOGE("sensors vhal receive sensor header message failed: %s ", strerror(errno));
        return;
    }
    switch (sensor_events_header.type) {
        case SENSOR_TYPE_ACCELEROMETER:
            payload_len = 3 * sizeof(float);
            break;
        case SENSOR_TYPE_GYROSCOPE:
            payload_len = 3 * sizeof(float);
            break;
        case SENSOR_TYPE_MAGNETIC_FIELD:
            payload_len = 3 * sizeof(float);
            break;
        default:
            payload_len = 0;
            ALOGW("unsupported sensor type %d", sensor_events_header.type);
            return;
    }

    if (m_msg_mem_pool.empty()) {
        ALOGI("pool run out, create new buffer");
        std::unique_lock<std::mutex> lock(m_msg_pool_mtx);
        int buf_size = sizeof(aic_sensors_event_t) + MAX_SENSOR_PAYLOAD_SIZE;
        m_msg_mem_pool.emplace(make_unique_ptr<std::vector<char>>(buf_size));
    }

    aic_sensors_event_t* sensor_events_ptr = nullptr;
    std::unique_ptr<std::vector<char>> buf_ptr;
    {
        std::unique_lock<std::mutex> lock(m_msg_pool_mtx);
        buf_ptr = std::move(m_msg_mem_pool.front());
        m_msg_mem_pool.pop();
    }
    sensor_events_ptr = (aic_sensors_event_t*)buf_ptr->data();
    memcpy(sensor_events_ptr, &sensor_events_header, sizeof(aic_sensors_event_t));
    len = m_socket_server->recv_data(client, sensor_events_ptr->data.fdata, payload_len, SOCK_BLOCK_MODE);

    if (len <= 0) {
        ALOGE("sensors vhal receive sensor data failed: %s", strerror(errno));
        return;
    }

    {
        std::unique_lock<std::mutex> lck(m_msg_queue_mtx);
        while (m_sensor_msg_queue.size() >= MAX_MSG_QUEUE_SIZE) {
            ALOGW("the sensor message queue is full, drop the old data...");
            m_msg_mem_pool.emplace(std::move(m_sensor_msg_queue.front()));
            m_sensor_msg_queue.pop();
        }
        m_sensor_msg_queue.emplace(std::move(buf_ptr));
        m_msg_queue_ready_cv.notify_all();
    }
}

void SensorDevice::client_connected_callback(SockServer* sock, sock_client_proxy_t* client) {
    ALOGD("sensor client connected to vhal successfully");
    for (int i = 0; i < MAX_NUM_SENSORS; i++) {
        m_mutex.lock();
        sensor_device_send_config_msg(m_sensor_config_status + i, sizeof(sensor_config_msg_t));
        m_mutex.unlock();
    }
}

static int sensor_poll_events(struct sensors_poll_device_t* dev0, sensors_event_t* data, int count) {
    if (count <= 0) {
        return -EINVAL;
    }
    SensorDevice* dev = (SensorDevice*)dev0;
    return dev->sensor_device_poll(data, count);
}

static int sensor_activate(struct sensors_poll_device_t* dev0, int handle, int enabled) {
    SensorDevice* dev = (SensorDevice*)dev0;
    return dev->sensor_device_activate(handle, enabled);
}

static int sensor_batch(struct sensors_poll_device_1* dev0, int handle, int flags __unused, int64_t sampling_period_ns, int64_t max_report_latency_ns __unused) {
    SensorDevice* dev = (SensorDevice*)dev0;
    return dev->sensor_device_batch(handle, sampling_period_ns);
}

static int sensor_set_delay(struct sensors_poll_device_t* dev0, int handle __unused, int64_t ns) {
    SensorDevice* dev = (SensorDevice*)dev0;
    return dev->sensor_device_set_delay(handle, ns);
}

static int sensor_flush(struct sensors_poll_device_1* dev0, int handle) {
    SensorDevice* dev = (SensorDevice*)dev0;
    return dev->sensor_device_flush(handle);
}

static int sensor_close(struct hw_device_t* dev0) {
    ALOGI("close sensor device");
    SensorDevice* dev = (SensorDevice*)dev0;
    delete dev;
    dev = nullptr;
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
    {.name                   = "AIC 3-axis Accelerometer",
     .vendor                 = "Intel ACGSS",
     .version                = 1,
     .handle                 = ID_ACCELERATION,
     .type                   = SENSOR_TYPE_ACCELEROMETER,
     .maxRange               = 2.8f,
     .resolution             = 1.0f / 4032.0f,
     .power                  = 3.0f,
     .minDelay               = 10000,
     .maxDelay               = 500 * 1000,
     .fifoReservedEventCount = 0,
     .fifoMaxEventCount      = 0,
     .stringType             = "android.sensor.accelerometer",
     .requiredPermission     = 0,
     .flags                  = SENSOR_FLAG_CONTINUOUS_MODE,
     .reserved               = {}},

    {.name       = "AIC 3-axis Gyroscope",
     .vendor     = "Intel ACGSS",
     .version    = 1,
     .handle     = ID_GYROSCOPE,
     .type       = SENSOR_TYPE_GYROSCOPE,
     .maxRange   = 11.1111111,
     .resolution = 1.0f / 1000.0f,
     .power      = 3.0f,
     .minDelay   = 10000,
     .maxDelay   = 500 * 1000,
     .stringType = "android.sensor.gyroscope",
     .reserved   = {}},

    {.name                   = "AIC 3-axis Magnetic field sensor",
     .vendor                 = "Intel ACGSS",
     .version                = 1,
     .handle                 = ID_MAGNETIC_FIELD,
     .type                   = SENSOR_TYPE_MAGNETIC_FIELD,
     .maxRange               = 2000.0f,
     .resolution             = 1.0f,
     .power                  = 6.7f,
     .minDelay               = 10000,
     .maxDelay               = 500 * 1000,
     .fifoReservedEventCount = 0,
     .fifoMaxEventCount      = 0,
     .stringType             = "android.sensor.magnetic_field",
     .requiredPermission     = 0,
     .flags                  = SENSOR_FLAG_CONTINUOUS_MODE,
     .reserved               = {}},
};

static int sensors__get_sensors_list(struct sensors_module_t* module __unused, struct sensor_t const** list) {
    *list = sSensorListInit;
    ALOGD("get sensor list, support %d sensors", MAX_NUM_SENSORS);
    return MAX_NUM_SENSORS;
}

static int open_sensors(const struct hw_module_t* module, const char* name, struct hw_device_t** device) {
    int status = -EINVAL;
    ALOGD("open_sensors");
    if (!strcmp(name, SENSORS_HARDWARE_POLL)) {
        SensorDevice* dev = new SensorDevice();

        // memset(dev, 0, sizeof(*dev));
        dev->device.common.tag     = HARDWARE_DEVICE_TAG;
        dev->device.common.version = SENSORS_DEVICE_API_VERSION_1_3;
        dev->device.common.module  = (struct hw_module_t*)module;
        dev->device.common.close   = sensor_close;
        dev->device.poll           = sensor_poll_events;
        dev->device.activate       = sensor_activate;
        dev->device.setDelay       = sensor_set_delay;

        // (dev->sensors[i].type == SENSOR_TYPE_META_DATA) is
        // sticky. Don't start off with that setting.
// Version 1.3-specific functions
#if defined(SENSORS_DEVICE_API_VERSION_1_3)
        dev->device.batch = sensor_batch;
        dev->device.flush = sensor_flush;
#endif
        *device = &dev->device.common;
        status  = 0;
    };
    return status;
}

static struct hw_module_methods_t sensors_module_methods = {.open = open_sensors};

struct sensors_module_t HAL_MODULE_INFO_SYM = {.common =
                                                   {
                                                       .tag           = HARDWARE_MODULE_TAG,
                                                       .version_major = 1,
                                                       .version_minor = 3,
                                                       .id            = SENSORS_HARDWARE_MODULE_ID,
                                                       .name          = "AIC SENSORS Module",
                                                       .author        = "Intel ACGSS",
                                                       .methods       = &sensors_module_methods,
                                                   },
                                               .get_sensors_list = sensors__get_sensors_list};
