# Build Remote Sensor HAL
1. copy the project folder to $AIC_SOURCE_FOLDER/hardware/intel/
2. build sensor hal
---
    cd $AIC_SOURCE_FOLDER 
    sourece ./build/env_setup.sh
    lunch aosp_x86_64-eng
    mmm ./hardware/intel/aic_sensor_hal/

# Test Remote Sensor HAL
* test sensor hal with system  
---
    cp $AIC_SOURCE_FOLDER/out/target/product/generic_x86_64/system/lib64/hw/sensors.cic_cloud.so $AIC_FOLDER/update/root/system/vendor/lib64/hw/
    cd $AIC_FOLDER
    ./aic install -u
    ./aic start
