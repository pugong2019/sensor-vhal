/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/select.h>
#include <dlfcn.h>
#include <sys/socket.h>


#include <cutils/log.h>

#include "SocketSensor.h"

#define AKMD_DEFAULT_INTERVAL	200000000

#define COMMAND_PORT 7770
#define DATA_PORT 7771
#define QUEUE 20

/*****************************************************************************/

SocketSensor::SocketSensor()
: SensorBase(NULL, NULL),
      mEnabled(0),
{
	for (int i=0; i<numSensors; i++) {
		mEnabled[i] = 0;
		mDelay[i] = -1;
	}
    memset(mPendingEvents, 0, sizeof(mPendingEvents));

    mPendingEvents[Accelerometer].version = sizeof(sensors_event_t);
    mPendingEvents[Accelerometer].sensor = ID_A;
    mPendingEvents[Accelerometer].type = SENSOR_TYPE_ACCELEROMETER;
    mPendingEvents[Accelerometer].acceleration.status = SENSOR_STATUS_ACCURACY_HIGH;

    mPendingEvents[MagneticField].version = sizeof(sensors_event_t);
    mPendingEvents[MagneticField].sensor = ID_M;
    mPendingEvents[MagneticField].type = SENSOR_TYPE_MAGNETIC_FIELD;
    mPendingEvents[MagneticField].magnetic.status = SENSOR_STATUS_ACCURACY_HIGH;

    mPendingEvents[Orientation  ].version = sizeof(sensors_event_t);
    mPendingEvents[Orientation  ].sensor = ID_O;
    mPendingEvents[Orientation  ].type = SENSOR_TYPE_ORIENTATION;
    mPendingEvents[Orientation  ].orientation.status = SENSOR_STATUS_ACCURACY_HIGH;

    // TODO: Init socket waiting connect
    //

    // int command_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    // struct sockaddr_in command_sockaddr;
    // command_sockaddr.sin_family = AF_INET;
    // command_sockaddr.sin_port = htons(COMMAND_PORT);
    // command_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    // if(bind(command_socket_fd, (struct sockaddr* ) &command_sockaddr, sizeof(command_sockaddr))==-1) {
    //     perror("bind");
    //     exit(1);
    // }
    // if(listen(command_sockaddr, QUEUE) == -1) {
    //     perror("listen");
    //     exit(1);
    // }



    // int data_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    // struct sockaddr_in data_sockaddr;
    // data_sockaddr.sin_family = AF_INET;
    // data_sockaddr.sin_port = htons(DATA_PORT);
    // data_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);

    // if(bind(data_socket_fd, (struct sockaddr* ) &data_sockaddr, sizeof(data_sockaddr))==-1) {
    //     perror("bind");
    //     exit(1);
    // }
    // if(listen(data_socket_fd, QUEUE) == -1) {
    //     perror("listen");
    //     exit(1);
    // }



}

SocketSensor::~SocketSensor()
{
	for (int i=0; i<numSensors; i++) {
		setEnable(i, 0);
	}

	// TODO: close socket etc..
	//
}

int SocketSensor::getFd() const {
	if(data_fd == -1){
		ALOGE("Don't have data fd!!!");
	}

	return data_fd; 
}

int SocketSensor::setEnable(int32_t handle, int enabled)
{
	int id = handle2id(handle);
	int err = 0;
	char flag = NULL;

	switch (id) {
	case Accelerometer:
		break;
	case MagneticField:
		break;
	case Orientation:
		break;
	default:
		ALOGE("SocketSensor: unknown handle (%d)", handle);
		return -EINVAL;
	}


	if (mEnabled[id] <= 0) {
		if(enabled) {
			flag = true
		}
	} else if (mEnabled[id] == 1) {
		if(!enabled) {
			flag = false
		};
	}

    if (flag != NULL) {
    	
    	// TODO: set enable by socket
    	//


		if (err != 0) {
			return err;
		}
		ALOGD("SocketSensor: set %s to %s", id, flag);
    }

	if (enabled) {
		(mEnabled[id])++;
		if (mEnabled[id] > 32767) mEnabled[id] = 32767;
	} else {
		(mEnabled[id])--;
		if (mEnabled[id] < 0) mEnabled[id] = 0;
	}
	ALOGD("SocketSensor: mEnabled[%d] = %d", id, mEnabled[id]);

    return err;
}

int SocketSensor::setDelay(int32_t handle, int64_t ns)
{
	int id = handle2id(handle);
	int err = 0;
	char buffer[32];
	int bytes;

    if (ns < -1 || 2147483647 < ns) {
		ALOGE("SocketSensor: invalid delay (%lld)", ns);
        return -EINVAL;
	}

    switch (id) {
        case Accelerometer:
			break;
        case MagneticField:
			break;
        case Orientation:
			break;
		default:
			ALOGE("SocketSensor: unknown handle (%d)", handle);
			return -EINVAL;
    }

	if (ns != mDelay[id]) {
   		
   		// TODO2: set delay by socket
   		//

		if (err == 0) {
			mDelay[id] = ns;
			ALOGD("SocketSensor: set %s to %f ms.", id, ns/1000000.0f);
		}
	}

    return err;
}

int64_t SocketSensor::getDelay(int32_t handle)
{
	int id = handle2id(handle);
	if (id > 0) {
		return mDelay[id];
	} else {
		return 0;
	}
}

int SocketSensor::getEnable(int32_t handle)
{
	int id = handle2id(handle);
	if (id >= 0) {
		return mEnabled[id];
	} else {
		return 0;
	}
}

int SocketSensor::readEvents(sensors_event_t* data, int count)
{
	//Returns an array of sensor data by filling the data argument. 
	//This function must block until events are available. 
	//It will return the number of events read on success, or a negative error number in case of an error.

	//The number of events returned in data must be less or equal to the count argument. This function shall never return 0 (no event).
    
    if (count < 1)
        return -EINVAL;

    int numEventReceived = 0;
    
    //TODO: Read from socket
    //

    /*
    while (count && ..) {
        if (mEnabled[j]) {
            *data++ = mPendingEvents[j];
            count--;
            numEventReceived++;
        }
                
        
        } else {
            ALOGE("SocketSensor: unknown event (type=%d, code=%d)",
                    type, event->code);
        }
    }
    */
    return numEventReceived;
}


int SocketSensor::handle2id(int32_t handle)
{
    switch (handle) {
        case ID_A:
			return Accelerometer;
        case ID_M:
			return MagneticField;
        case ID_O:
			return Orientation;
		default:
			ALOGE("SocketSensor: unknown handle (%d)", handle);
			return -EINVAL;
    }
}
