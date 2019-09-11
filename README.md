# Build Remote Sensor HAL

1. copy the project folder to $AIC_SOURCE_FOLDER/hardware/intel/ 
2. build sensor hal
    ```
    cd $AIC_SOURCE_FOLDER
    mmm ./hardware/intel/remoteg-aic_sensor_hal/
    ```
# Test Remote Sensor HAL
   * test sensor hal with system
        ```
        cp $AIC_SOURCE_FOLDER/out/target/product/nuc/system/lib64/hw/sensors.default.so $AIC_FOLDER/update/root/system/lib64/hw
        cd $AIC_FOLDER
        ./aic install -u
        ./aic start
        ```


   * test sensor hal using bin
        ```
        adb root
        adb push $AIC_SOURCE_FOLDER/out/target/product/nuc/system/bin/hw/test_sensor /data/local/tmp/
        adb shell chmod 777 /data/local/tmp/test_sensor
        adb shell /data/local/tmp/test_sensor
        ```


# Remote Sensor Sequence diagram
![sequence](img/sequence.jpg)