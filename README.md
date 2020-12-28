# Build Remote Sensor HAL
1. copy the project folder to $AIC_SOURCE_FOLDER/hardware/intel/
2. build sensor hal
---
    cd $AIC_SOURCE_FOLDER 
    sourece ./build/env_setup.sh
    lunch aosp_x86_64-eng (If in cic_cloud source code: DIFF_VARIANT=game lunch cic_cloud-userdebug)
    mmm ./hardware/intel/aic_sensor_hal/

# Test Remote Sensor HAL
* test sensor hal with system  
---
    cp $AIC_SOURCE_FOLDER/out/target/product/cic_cloud/system/vendor/lib/hw/sensors.cic_cloud.so $AIC_FOLDER/update/root/system/vendor/lib/hw/
    cp $AIC_SOURCE_FOLDER/out/target/product/cic_cloud/system/vendor/lib64/hw/sensors.cic_cloud.so $AIC_FOLDER/update/root/system/vendor/lib64/hw/
    cd $AIC_FOLDER
    ./aic install -u
    ./aic start
