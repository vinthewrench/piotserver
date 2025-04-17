//
//  VELM6030.hpp
//  pIoTServer
//
//  Created by vinnie on 1/21/25.
//

// adapted from https://github.com/sparkfun/SparkFun_Ambient_Light_Sensor_Arduino_Library/tree/master

#ifndef VELM6030_hpp
#define VELM6030_hpp

#include <stdio.h>
#include "I2C.hpp"


// Table of lux conversion values depending on the integration time and gain.
// The arrays represent the all possible integration times and the index of the
// arrays represent the register's gain settings, which is directly analgous to
// their bit representations.
const float eightHIt[]     = {.0036, .0072, .0288, .0576};
const float fourHIt[]      = {.0072, .0144, .0576, .1152};
const float twoHIt[]       = {.0144, .0288, .1152, .2304};
const float oneHIt[]       = {.0288, .0576, .2304, .4608};
const float fiftyIt[]      = {.0576, .1152, .4608, .9216};
const float twentyFiveIt[] = {.1152, .2304, .9216, 1.8432};

class VELM6030
{
  public:

    VELM6030();
    ~VELM6030();

// address options
//    const uint8_t defAddr = 0x48;
//    const uint8_t altAddr = 0x10;

    
    bool begin(uint8_t deviceAddress = 0x10);
    bool begin(uint8_t deviceAddress,  int &error);

    
    void stop();
    bool isOpen();

    uint8_t    getDevAddr();

       // REG0x00, bits [12:11]
    // This function sets the gain for the Ambient Light Sensor. Possible values
    // are 1/8, 1/4, 1, and 2. The highest setting should only be used if the
    // sensors is behind dark glass, where as the lowest setting should be used in
    // dark rooms. The datasheet suggests always leaving it at around 1/4 or 1/8.
    bool setGain(float gainVal);

    // REG0x00, bits [12:11]
    // This function reads the gain for the Ambient Light Sensor. Possible values
    // are 1/8, 1/4, 1, and 2. The highest setting should only be used if the
    // sensors is behind dark glass, where as the lowest setting should be used in
    // dark rooms. The datasheet suggests always leaving it at around 1/4 or 1/8.
    bool readGain( float & gain);


    // REG0x00, bits[9:6]
    // This function sets the integration time (the saturation time of light on the
    // sensor) of the ambient light sensor. Higher integration time leads to better
    // resolution but slower sensor refresh times.
    bool setIntegTime(uint16_t intTime);

    // REG0x00, bits[9:6]
    // This function reads the integration time (the saturation time of light on the
    // sensor) of the ambient light sensor. Higher integration time leads to better
    // resolution but slower sensor refresh times.
    bool readIntegTime(uint16_t &intTime);

    // REG0x00, bits[5:4]
    // This function sets the persistence protect number i.e. the number of
    // values needing to crosh the interrupt thresholds.
//    void setProtect(uint8_t protVal);

    // REG0x00, bits[5:4]
    // This function reads the persistence protect number i.e. the number of
    // values needing to crosh the interrupt thresholds.
//    uint8_t readProtect();

    // REG0x00, bit[1]
    // This function enables the Ambient Light Sensor's interrupt.
    void enableInt();

    // REG0x00, bit[1]
    // This function disables the Ambient Light Sensor's interrupt.
//    void disableInt();

    // REG0x00, bit[1]
    // This function checks if the interrupt is enabled or disabled.
//    uint8_t readIntSetting();

    // REG0x00, bit[0]
    // This function powers down the Ambient Light Sensor. The light sensor will
    // hold onto the last light reading which can be acessed while the sensor is
    // shut down. 0.5 micro Amps are consumed while shutdown.
    bool shutDown();

    // REG0x00, bit[0]
    // This function powers up the Ambient Light Sensor. The last value that was
    // read during shut down will be overwritten on the sensor's subsequent read.
    // After power up, a small 4ms delay is applied to give time for the internal
    // osciallator and signal processor to power up.
    bool powerOn();

    // REG0x03, bit[0]
    // This function enables the current power save mode value and puts the Ambient
    // Light Sensor into power save mode.
    bool enablePowSave();

    // REG0x03, bit[0]
    // This function disables the current power save mode value and pulls the Ambient
    // Light Sensor out of power save mode.
    bool disablePowSave();

    // REG0x03, bit[0]
    // This function checks to see if power save mode is enabled or disabled.
    bool readPowSavEnabled(bool &isEnabled);
    
    // REG0x03, bit[2:1]
    // This function sets the power save mode value. It takes a value of 1-4. Each
    // incrementally higher value descreases the sampling rate of the sensor and so
    // increases power saving. The datasheet suggests enabling these modes when
    // continually sampling the sensor.
    bool setPowSavMode(uint16_t modeVal);

    // REG0x03, bit[2:1]
    // This function reads the power save mode value. The function above takes a value of 1-4. Each
    // incrementally higher value descreases the sampling rate of the sensor and so
    // increases power saving. The datasheet suggests enabling these modes when
    // continually sampling the sensor.
    uint8_t readPowSavMode();

    // REG0x06, bits[15:14]
    // This function reads the interrupt register to see if an interrupt has been
    // triggered. There are two possible interrupts: a lower limit and upper limit
    // threshold, both set by the user.
//    uint8_t readInterrupt();

    // REG0x02, bits[15:0]
    // This function sets the lower limit for the Ambient Light Sensor's interrupt.
    // It takes a lux value as its paramater.
 //   void setIntLowThresh(uint32_t luxVal);
    
    // REG0x02, bits[15:0]
    // This function reads the lower limit for the Ambient Light Sensor's interrupt.
//    uint32_t readLowThresh();

    // REG0x01, bits[15:0]
    // This function sets the upper limit for the Ambient Light Sensor's interrupt.
    // It takes a lux value as its paramater.
//    void setIntHighThresh(uint32_t luxVal);

    // REG0x01, bits[15:0]
    // This function reads the upper limit for the Ambient Light Sensor's interrupt.
 //   uint32_t readHighThresh();

    // REG[0x04], bits[15:0]
    // This function gets the sensor's ambient light's lux value. The lux value is
    // determined based on current gain and integration time settings. If the lux
    // value exceeds 1000 then a compensation formula is applied to it.
     bool readLight(uint32_t &luxOut);

    // REG[0x05], bits[15:0]
    // This function gets the sensor's ambient light's lux value. The lux value is
    // determined based on current gain and integration time settings. If the lux
    // value exceeds 1000 then a compensation formula is applied to it.
     bool readWhiteLight(uint32_t &luxOut);

  private:

    uint8_t _address;
    
    // This function compensates for lux values over 1000. From datasheet:
    // "Illumination values higher than 1000 lx show non-linearity. This
    // non-linearity is the same for all sensors, so a compensation forumla..."
    // etc. etc.
    uint32_t _luxCompensation(uint32_t _luxVal);

    // The lux value of the Ambient Light sensor depends on both the gain and the
    // integration time settings. This function determines which conversion value
    // to use by using the bit representation of the gain as an index to look up
    // the conversion value in the correct integration time array. It then converts
    // the value and returns it.
    bool _calculateLux(uint16_t _lightBits, uint32_t & calculatedLux );

    // This function does the opposite calculation then the function above. The interrupt
    // threshold values given by the user are dependent on the gain and
    // intergration time settings. As a result the lux value needs to be
    // calculated with the current settings and this function accomplishes
    // that.
    bool _calculateBits(uint32_t _luxVal, uint16_t & calculatedBits);

    // This function writes to a 16 bit register. Paramaters include the register's address, a mask
    // for bits that are ignored, the bits to write, and the bits' starting
    // position.
    bool _writeRegister(uint8_t _wReg, uint16_t _mask, uint16_t _bits, uint8_t _startPosition);


    I2C         _i2cPort;
    bool        _isSetup;

};
 #endif /* VELM6030_hpp */
