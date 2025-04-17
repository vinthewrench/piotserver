//
//  SHT25.hpp
//  SHT25
//
//  Created by vinnie on 3/12/25.
//

#ifndef SHT25_hpp
#define SHT25_hpp

#include <stdio.h>
#include "I2C.hpp"

class SHT25
{
    
public:
    
    struct SHT25_data
    {
        double temperature;
  
        double humidity;
     };
    
    SHT25();
    ~SHT25();
   
    static const uint8_t SHT25_DEFAULT_ADDR  = 0x40;
 
    bool begin(uint8_t deviceAddress = SHT25_DEFAULT_ADDR);
    bool begin(uint8_t deviceAddress,  int &error);
    
    void stop();
    bool isOpen();
    
    uint8_t getDevAddr();
 
    bool softReset();
    
    bool readSensor(SHT25_data &data);
 
    bool readSerialNumber(uint8_t serialNo[8]);
    
private:
    
  
    I2C         _i2cPort;
    bool        _isSetup;
    
};
#endif /* SHT25_hpp */
