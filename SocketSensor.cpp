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

#include <arpa/inet.h>

#include "SocketSensor.h"

#ifdef BIN
    #define COMMAND_PORT 4321
    #define DATA_PORT 1234
#else
    #define COMMAND_PORT 4321
    #define DATA_PORT 1234
#endif


#define TCP_CLIENT_QUEUE 20

#define TIMEOUT_US 2000

// #define SEVER_IP "10.42.0.117"
#define SEVER_IP "172.0.0.1"
#define LOCAL_IP "127.0.0.1"


struct sockaddr_in ser_addr; 
struct sockaddr_in client_addr;

struct sockaddr_in command_my_sockaddr;
struct sockaddr_in command_rt_sockaddr;

bool tcp_connected = false;

socklen_t client_addr_len = sizeof(client_addr);

int recieved_len = 0;
int SocketSensor::command_conn_fd = -1;
char buf[100];
typedef struct{
    int type;
    int accuracy;
    int mode;
    int status;
} ctl_msg_t;

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
    mPendingEvents[Orientation  ].sensor = ID_G;
    mPendingEvents[Orientation  ].type = SENSOR_TYPE_ORIENTATION;
    mPendingEvents[Orientation  ].orientation.status = SENSOR_STATUS_ACCURACY_HIGH;

    // Init socket waiting connect
    //
    int ret = 0;
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
    ALOGE("cp: udp socket bind success = %d!\n",DATA_PORT);
    printf("cp: create udp thread success = %d!\n",DATA_PORT);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = TIMEOUT_US;
    if(setsockopt(data_socket_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0){
        ALOGE("SocketSensor: set socketopt error\n");
    }
    ALOGD("SocketSensor: client tcp connected , tcpfd = %d , udpfd = %d", command_conn_fd , data_socket_fd);
    /* 
    while(true){
        while(tcp_connected){
            int tcp = read(command_socket_fd,buf,sizeof(buf));
            if(tcp > 0){
                printf("recv tcp data = %d \n",tcp);
            }
            int udp = recvfrom(data_socket_fd,buf,sizeof(buf),0,(struct sockaddr*)&ser_addr,&client_addr_len);
            if(udp > 0){
                printf("recv udp data = %d \n",udp);
            }
        }
    }
    */
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
    int enable = 1;
	int i = 0;
    struct	sockaddr_in command_client_sockaddr;

    command_conn_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(command_conn_fd < 0)
    {
        printf("creat tcp fd error\n");
        ALOGE("SocketSensor: create command socket fail!\n");
    }
    memset(&command_client_sockaddr,0,sizeof(command_client_sockaddr));
    command_client_sockaddr.sin_family = AF_INET;
    command_client_sockaddr.sin_port = htons(COMMAND_PORT);
    command_client_sockaddr.sin_addr.s_addr = inet_addr(LOCAL_IP);
    printf("bind port: %d??\n", COMMAND_PORT);
    fflush(stdout);

    printf("tcp client start to connect\n");


    ALOGD("SocketSensor: connect client tcp");


	int ret = connect(command_conn_fd, (struct sockaddr *)&command_client_sockaddr, sizeof(command_client_sockaddr));
	while (ret < 0 && ++i < 3) {
		ret = connect(command_conn_fd, (struct sockaddr *)&command_client_sockaddr, sizeof(command_client_sockaddr));
	}
    if(ret < 0)
        return 0;
    tcp_connected = true;

    printf("SocketSensor: client tcp connected\n");
    ALOGD("SocketSensor: client tcp connected = %d",command_conn_fd);
    while(1){
        sleep(10);
    }
    return 0;
}
/* 
void * SocketSensor:: tcpThread(void *){
    //Init TCP server, command socket
    int enable = 1;
    struct	sockaddr_in command_client_sockaddr;
    socklen_t l = sizeof(command_client_sockaddr);
    int command_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(command_socket_fd < 0)
    {
        printf("creat tcp fd error\n");
        ALOGE("SocketSensor: create command socket fail!\n");
    }
    memset(&command_my_sockaddr,0,sizeof(command_my_sockaddr));
    command_my_sockaddr.sin_family = AF_INET;
    command_my_sockaddr.sin_port = htons(COMMAND_PORT);
    command_my_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    printf("bind port: %d??\n", COMMAND_PORT);
    fflush(stdout);

    if (setsockopt(command_socket_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
		printf("[err] command_socket_fd reuse\n");
		close(command_socket_fd);
	}

    if(bind(command_socket_fd, (struct sockaddr* ) &command_my_sockaddr, sizeof(command_my_sockaddr))==-1) {
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


    if( (command_conn_fd = accept(command_socket_fd, (struct sockaddr *)&command_client_sockaddr, &l)) == -1) { 
        printf(" accpt socket error: %s (errno :%d)\n",strerror(errno),errno); 
        ALOGD("SocketSensor: accpt failed");

        //return 0; 
    }

    tcp_connected = true;

    printf("SocketSensor: client tcp connected\n");
    ALOGD("SocketSensor: client tcp connected = %d",command_conn_fd);
    while(1){
        sleep(10);
    }
    return 0;
}
*/
// void * SocketSensor:: tcpThread(void *){
//     //Init TCP client, command socket

//     ALOGD("createTcpSocket...");
//     int command_socket_fd;
//     bzero(&command_my_sockaddr, sizeof(command_my_sockaddr));
//     bzero(&command_rt_sockaddr, sizeof(command_rt_sockaddr));
//     const char *server = SEVER_IP;  // 服务器IP

//     if ((command_socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0)) < 0)
//         ALOGE("SocketSensor: create tcp socket error: %s(errno: %d)\n", strerror(errno), errno);

//     memset((char *) &command_my_sockaddr, 0, sizeof(command_my_sockaddr));
//     command_my_sockaddr.sin_family = AF_INET;
//     command_my_sockaddr.sin_addr.s_addr = htonl(INADDR_ANY);
//     command_my_sockaddr.sin_port = htons(0);

//     memset((char *) &command_rt_sockaddr, 0, sizeof(command_rt_sockaddr));
//     command_rt_sockaddr.sin_family = AF_INET;
//     command_rt_sockaddr.sin_port = htons(COMMAND_PORT);

//     int on=0;
//     if((setsockopt(command_socket_fd, SOL_SOCKET,SO_REUSEADDR, &on, sizeof(on)))<0)
//     {
//         perror("SocketSensor: setsockopt failed");
//     }

//     if (inet_pton(AF_INET, server, &command_rt_sockaddr.sin_addr) <= 0) {
//         ALOGE("SocketSensor: tcp bind error: %s(errno: %d)\n", strerror(errno), errno);
//     }

//     if (connect(command_socket_fd, (struct sockaddr *)&command_rt_sockaddr, sizeof(command_rt_sockaddr)) < 0)
//     {
//         ALOGE("SocketSensor: tcp connect error: %s(errno: %d)\n", strerror(errno), errno);
//     }
//     tcp_connected = true;
//     ALOGI("SocketSensor: TCP connected.");
//     while(1){
//         sleep(10);
//     }
    
// }

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
    int type = 0;
    char send_buf[20];
    ALOGE("SocketSensor: seeeeeeet enable");
	switch (id) {
	case Accelerometer:
        type = 1;
		break;
	case MagneticField:
        type = 2;
		break;
	case Orientation:
        type = 4;
		break;
	default:
		ALOGE("SocketSensor: unknown handle (%d)", handle);
		return -EINVAL;
	}


    printf("SocketSensor: start to set handle %d Enable\n", handle);


    if (!tcp_connected){
        ALOGD("SocketSensor: client is not connected, enable failed");
        printf("SocketSensor: client is not connected, enable failed\n");
        return -1;

    }
    else{
        ALOGD("SocketSensor: client is connected, enable success = %d",command_conn_fd);
        printf("SocketSensor: client is connected, enable success\n");
        ctl_msg_t * ctl_msg = (ctl_msg_t*)send_buf;
        ctl_msg->type = type;
        ctl_msg->accuracy = 0;
        ctl_msg->mode = 3;
        ctl_msg->status = 1;
        int wr_len = send(command_conn_fd,send_buf,sizeof(send_buf),0);
        ALOGD("SocketSensor: send enable msg (%d)", wr_len);
        printf("SocketSensor: send enable msg = d%\n",wr_len);
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
    int type = 0;
    char send_buf[20];
    ALOGE("SocketSensor: seeeeeeet delay");
    if (ns < -1 || 2147483647 < ns) {
		ALOGE("SocketSensor: invalid delay (%lld)", ns);
        return -EINVAL;
	}

    switch (id) {
        case Accelerometer:
            type = 1;
		    break;
	    case MagneticField:
            type = 2;
		    break;
	    case Orientation:
            type = 4;
		    break;
		default:
			ALOGE("SocketSensor: unknown handle (%d)", handle);
			return -EINVAL;
    }

    if (tcp_connected){
        ALOGD("SocketSensor: client is connected, enable success");
        printf("SocketSensor: client is connected, enable success\n");
        ctl_msg_t * ctl_msg = (ctl_msg_t*)send_buf;
        ctl_msg->type = type;
        ctl_msg->accuracy = 1;
        ctl_msg->mode = 0;
        ctl_msg->status = 0;
        int wr_len = write(command_conn_fd,send_buf,sizeof(send_buf));
        ALOGD("SocketSensor: send delay msg (%d)", wr_len);
        printf("SocketSensor: send delay msg = %d\n",wr_len);
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
    
    // ALOGD("SocketSensor: check client connected");

    if (count < 1)
        return -EINVAL;

    int numEventReceived = 0;
    int buffer_lenth = count*sizeof(sensors_event_t);


    //ALOGD("SocketSensor: readEvents, wait %d data from UDP", count);

    recieved_len = recvfrom(data_socket_fd, data, buffer_lenth, 0, (struct sockaddr*)&client_addr, &client_addr_len);  //unblocked
    //int rec = read(command_socket_fd,data,buffer_lenth);
	//ALOGE("SocketSensor: recieved data: %d, package size: %d", recieved_len, sizeof(sensors_event_t));
    //ALOGE("SocketSensor: recieved data: (version, %d) (sensor, %d) (type, %d) (timestamp %lld)", data[0].version, data[0].sensor, data[0].type, data[0].timestamp);
    //ALOGE("SocketSensor: recieved data: (version, %s) (sensor, %d) (type, %d) (timestamp, %d)", data->version, data->sensor, data->type, data->timestamp);
    numEventReceived = recieved_len/sizeof(sensors_event_t);

    if(recieved_len < 0){
        //ALOGD("SocketSensor: receive time out");
        printf("recieve time out!\n");
        numEventReceived = 0;
    }
    else{
        //ALOGD("SocketSensor: receive udp data = %d,=%d",recieved_len,numEventReceived);
        //printf("recieved udp data\n");
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
        case ID_G:
			return Orientation;
		default:
			ALOGE("SocketSensor: unknown handle (%d)", handle);
			return -EINVAL;
    }
}
