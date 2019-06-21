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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>

#include <cutils/log.h>

#include "SocketSensor.h"

#ifdef BIN
    #define COMMAND_PORT 7770
    #define DATA_PORT 7772
#else
    #define COMMAND_PORT 6770
    #define DATA_PORT 6772
#endif


#define TCP_CLIENT_QUEUE 20

#define BUFF_LEN 65536

char sensor_data_buffer[BUFF_LEN];  //recived buffer
struct sockaddr_in ser_addr; 
struct sockaddr_in client_addr;

struct sockaddr_in command_sockaddr;


socklen_t client_addr_len = sizeof(client_addr);

int recieved_len = 0;
int SocketSensor::command_conn_fd = -1;

/*****************************************************************************/

SocketSensor::SocketSensor()
	: SensorBase(NULL, NULL)
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

    // Init socket waiting connect
    //
    int ret;

    pthread_t id;
    ret = pthread_create(&id, NULL, tcpThread, NULL);
    if(ret != 0){
        printf("SocketSensor: create tcp thread failed!\n");

        ALOGE("SocketSensor: create tcp thread failed!\n");
    }


    data_socket_fd = socket(AF_INET, SOCK_DGRAM, 0); //AF_INET:IPV4;SOCK_DGRAM:UDP
    if(data_socket_fd < 0)
    {
        ALOGE("SocketSensor: create data socket fail!\n");
    }
    ser_addr.sin_family = AF_INET;
    ser_addr.sin_addr.s_addr = htonl(INADDR_ANY); //IP address，INADDR_ANY：local address
    ser_addr.sin_port = htons(DATA_PORT);  //port number

    ret = bind(data_socket_fd, (struct sockaddr*)&ser_addr, sizeof(ser_addr));
    if(ret < 0)
    {
        ALOGE("SocketSensor: socket bind fail!\n");
    }

}

SocketSensor::~SocketSensor()
{
	for (int i=0; i<numSensors; i++) {
		setEnable(i, 0);
	}

	// TODO: close socket etc..
	//
    close(data_socket_fd);

}

void * SocketSensor:: tcpThread(void *){
    //Init TCP server, command socket
    int command_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(command_socket_fd < 0)
    {
        ALOGE("SocketSensor: create command socket fail!\n");
    }
    command_sockaddr.sin_family = AF_INET;
    command_sockaddr.sin_port = htons(COMMAND_PORT);
    command_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    printf("bind port: %d??\n", COMMAND_PORT);
    fflush(stdout);

    if(bind(command_socket_fd, (struct sockaddr* ) &command_sockaddr, sizeof(command_sockaddr))==-1) {
        printf("SocketSensor: bind failed");

        perror("bind");

        ALOGD("SocketSensor: bind failed");

        //exit(1);
    }

    printf("tcp sever start to listen\n");

    if(listen(command_socket_fd, TCP_CLIENT_QUEUE) == -1) {
        perror("listen");
        ALOGD("SocketSensor: listen failed");

        //exit(1);
    }

    printf("tcp sever start to wait and accept\n");


    ALOGD("SocketSensor: waitting for client tcp connected");


    if( (command_conn_fd = accept(command_socket_fd, (struct sockaddr *)NULL, NULL)) == -1) { 
        printf(" accpt socket error: %s (errno :%d)\n",strerror(errno),errno); 
        ALOGD("SocketSensor: accpt failed");

        //return 0; 
    }
    ALOGD("SocketSensor: client tcp connected");

    printf("SocketSensor: client tcp connected\n");

    while(1){
        sleep(10);
    }

    return 0;
}

int SocketSensor::getFd() const {
	if(data_fd == -1){
		ALOGE("SocketSensor: Don't have data fd!!!");
	}

	return data_fd; 
}

int SocketSensor::setEnable(int32_t handle, int enabled)
{
	int id = handle2id(handle);
	int err = 0;
	bool flag = 0;

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


    printf("SocketSensor: start to set Enable\n");


    if (command_conn_fd == -1){
        ALOGD("SocketSensor: client is not connected, enable failed");
        printf("SocketSensor: client is not connected, enable failed\n");
        return -1;
    }

    // TODO: fix bug
	// if (mEnabled[id] <= 0) {
	// 	if(enabled) {
	// 		flag = 1;
	// 	}
	// } else if (mEnabled[id] == 1) {
	// 	if(!enabled) {
	// 		flag = 2;
	// 	};
	// }

    if (flag != 0) {
    	
    	// TODO: set enable by socket
    	//

		if (err != 0) {
			return err;
		}
		ALOGD("SocketSensor: set %d to %s", id, flag);
        printf("SocketSensor: set %d to %s\n", id, flag);

    }

    // TODO: fix bug
	// if (enabled) {
	// 	(mEnabled[id])++;
	// 	if (mEnabled[id] > 32767) mEnabled[id] = 32767;
	// } else {
	// 	(mEnabled[id])--;
	// 	if (mEnabled[id] < 0) mEnabled[id] = 0;
	// }
    
    mEnabled[id] = 1;
	ALOGD("SocketSensor: mEnabled[%d] = %d", id, mEnabled[id]);
    printf("SocketSensor: mEnabled[%d] = %d\n", id, mEnabled[id]);

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

    //TODO: setDelay by TCP
    //
    //
    //

	// if (ns != mDelay[id]) {
   		
 //   		// TODO2: set delay by socket
 //   		//

	// 	if (err == 0) {
	// 		mDelay[id] = ns;
	// 		ALOGD("SocketSensor: set %s to %f ms.", id, ns/1000000.0f);
	// 	}
	// }

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
    
    ALOGD("SocketSensor: check client connected");

    while(command_conn_fd == -1){
        ALOGD("SocketSensor: readEvents, client isn't connected");
        sleep(5);
    }

    if (count < 1)
        return -EINVAL;

    int numEventReceived = 0;
    int buffer_lenth = count*sizeof(sensors_event_t);

    memset(sensor_data_buffer, 0, buffer_lenth);

    ALOGD("SocketSensor: readEvents, wait %d data from UDP", count);

    //recieved_len = recvfrom(data_socket_fd, sensor_data_buffer, buffer_lenth, 0, (struct sockaddr*)&client_addr, &client_addr_len);  //blocked
    recieved_len = recvfrom(data_socket_fd, data, buffer_lenth, 0, (struct sockaddr*)&client_addr, &client_addr_len);  //blocked

	
    // *data = sensor_data_buffer;
	ALOGE("SocketSensor: recieved data: %d, package size: %d", recieved_len, sizeof(sensors_event_t));
    numEventReceived = recieved_len/sizeof(sensors_event_t);

    if(recieved_len == 0){
        printf("recieve data fail!\n");
        numEventReceived = 0;
    }

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
