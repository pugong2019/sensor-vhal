#include <SocketSensor.h>
#include <SensorBase.h>

#include <stdio.h>

#include <stdlib.h>

#include <unistd.h>


int main(int argc, char** argv)
{
    int err = 0;
    int ret = 0;

    sensors_event_t *sensor_data = (sensors_event_t *)malloc(4* sizeof(sensors_event_t));
    SensorBase* mSensors;

    mSensors = new SocketSensor();

	printf("enable:%d??\n",err);


    mSensors->setEnable(0, 0);

    printf("read Event\n");

    int nb = mSensors->readEvents(sensor_data, 4);

    printf("set enable while\n");
    
    do{
        ret = mSensors->setEnable(0, 1);
        sleep(5);
    }while(ret != 0);

    printf("read event while\n");

    nb = 1;
	while(true){
	    int nb = mSensors->readEvents(sensor_data, 4);
    	printf("get %d data\n",nb);
	}


    return 0;
}