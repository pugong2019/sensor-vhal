#include <SocketSensor.h>
#include <SensorBase.h>

#include <stdio.h>

#include <stdlib.h>

int main(int argc, char** argv)
{
    int err;
    sensors_event_t *sensor_data = (sensors_event_t *)malloc(4* sizeof(sensors_event_t));
    SensorBase* mSensors;

    mSensors = new SocketSensor();

	printf("enable:%d\n",err);

	while(true){
	    int nb = mSensors->readEvents(sensor_data, 4);
    	printf("get %d data\n",nb);
    	printf("%d\n", sensor_data[0].type);
	}


    return 0;
}