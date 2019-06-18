#include <SocketSensor.h>
#include <SensorBase.h>

#include <stdio.h>

#include <stdlib.h>

#include <unistd.h>


int main(int argc, char** argv)
{
    int err;
    int ret = 0;

    sensors_event_t *sensor_data = (sensors_event_t *)malloc(4* sizeof(sensors_event_t));
    SensorBase* mSensors;

    mSensors = new SocketSensor();

	printf("enable:%d??\n",err);


    mSensors->setEnable(0, 0);
    int nb = mSensors->readEvents(sensor_data, 4);


    do{
        ret = mSensors->setEnable(0, 1);
        sleep(1);
    }while(ret != 0);


	while(true){
	    int nb = mSensors->readEvents(sensor_data, 4);
    	printf("get %d data\n",nb);
    	printf("%d\n", sensor_data[0].type);
	}


    return 0;
}