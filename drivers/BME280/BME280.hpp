//
//  BME280.hpp
//  bmetest
//
//  Created by vinnie on 12/8/24.
//

#ifndef bme280_hpp
#define bme280_hpp

#include <stdio.h>
#include "I2C.hpp"

struct bme280_calib_data
{
    /*! Calibration coefficient for the temperature sensor */
    uint16_t dig_t1;

    /*! Calibration coefficient for the temperature sensor */
    int16_t dig_t2;

    /*! Calibration coefficient for the temperature sensor */
    int16_t dig_t3;

    /*! Calibration coefficient for the pressure sensor */
    uint16_t dig_p1;

    /*! Calibration coefficient for the pressure sensor */
    int16_t dig_p2;

    /*! Calibration coefficient for the pressure sensor */
    int16_t dig_p3;

    /*! Calibration coefficient for the pressure sensor */
    int16_t dig_p4;

    /*! Calibration coefficient for the pressure sensor */
    int16_t dig_p5;

    /*! Calibration coefficient for the pressure sensor */
    int16_t dig_p6;

    /*! Calibration coefficient for the pressure sensor */
    int16_t dig_p7;

    /*! Calibration coefficient for the pressure sensor */
    int16_t dig_p8;

    /*! Calibration coefficient for the pressure sensor */
    int16_t dig_p9;

    /*! Calibration coefficient for the humidity sensor */
    uint8_t dig_h1;

    /*! Calibration coefficient for the humidity sensor */
    int16_t dig_h2;

    /*! Calibration coefficient for the humidity sensor */
    uint8_t dig_h3;

    /*! Calibration coefficient for the humidity sensor */
    int16_t dig_h4;

    /*! Calibration coefficient for the humidity sensor */
    int16_t dig_h5;

    /*! Calibration coefficient for the humidity sensor */
    int8_t dig_h6;

    /*! Variable to store the intermediate temperature coefficient */
    int32_t t_fine;
};

/*! @name Sensor power modes */
#define BME280_POWERMODE_SLEEP                    UINT8_C(0x00)
#define BME280_POWERMODE_FORCED                   UINT8_C(0x01)
#define BME280_POWERMODE_NORMAL                   UINT8_C(0x03)
 

class BME280
{
    
public:
    
    struct compensated_data
    {
        /*! Compensated pressure  in Hectopascal */
        /* The BME280 can measure pressure between 300Pa to 1100 hPa with an absolute
         accuracy of ±1 hPa.
         
         Over the temperature range of 0 to 65°C, full accuracy is obtained,
         resulting in an altitude measurement accuracy of approximately ±1 meter.
         Outside of that range, the accuracy drops to 1.7 hPa.*/
        
        double pressure;
        
        /*! Compensated temperature - degrees C */
        /* he BME280 can measure temperatures ranging from -40°C to 85°C.
         Over the temperature range of 0 to 65°C, the accuracy is ±1.0°C;
         outside of that range, the accuracy drops to ±1.5°C.
         */
        double temperature;
        
        /*! Compensated humidity */
        /* The BME280 can measure relative humidity over a range of 0 to 100%
         with an accuracy of ±3%.
         
         According to the datasheet, the sensor can measure up to 100% humidity
         over a temperature range of 0 to 60°C. However, the maximum measurable
         humidity decreases at extremely high and low temperatures.*/
        double humidity;
    };
    
    BME280();
    ~BME280();
    
    // Address of BME280 sensor (0x77)
    
    bool begin(uint8_t deviceAddress = 0x77);
    bool begin(uint8_t deviceAddress,  int &error);
    
    void stop();
    bool isOpen();
    
     uint8_t getDevAddr();
    
    bool readSensor(compensated_data &data);
    
private:
    bool getChipID(uint8_t &chipID);
  
    bool softReset();
    bool getCalibrationData();
    
    /*    sensor_mode       |      Macros
     * ---------------------|-------------------------
     *     0                | BME280_POWERMODE_SLEEP
     *     1                | BME280_POWERMODE_FORCED
     *     3                | BME280_POWERMODE_NORMAL
     */
    
    bool  setPowerMode(uint8_t powerMode);
    bool  getPowerMode(uint8_t &powerMode);
    
    bool configureForWeather();
    bool processSensorData(uint8_t *regData, compensated_data& dataOut);
    
    I2C         _i2cPort;
    bool        _isSetup;
    
    
    bme280_calib_data   _calib_data;
};

 
#endif /* bme280_hpp */
