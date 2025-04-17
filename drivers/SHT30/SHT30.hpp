//
//  SHT30.hpp
//  sht30
//
//  Created by vinnie on 3/12/25.
//

#ifndef SHT30_hpp
#define SHT30_hpp

#include <stdio.h>
#include "I2C.hpp"

class SHT30
{
    
public:
    
    struct SHT30_data
    {
        double temperature;
  
        double humidity;
     };
    
    SHT30();
    ~SHT30();
    
    // Address of SHT30 sensor (0x44)
    
    bool begin(uint8_t deviceAddress = 0x44);
    bool begin(uint8_t deviceAddress,  int &error);
    
    void stop();
    bool isOpen();
    
    uint8_t getDevAddr();
 
    bool readSensor(SHT30_data &data);
 
    bool readSerialNumber(uint8_t serialNo[8]);
 
    bool softReset();

private:
    
    bool init();
  
    I2C         _i2cPort;
    bool        _isSetup;
    
};
#endif /* SHT30_hpp */
