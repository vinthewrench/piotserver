//
//  ADS1115.hpp
//  ncd
//
//  Created by vinnie on 4/2/25.
//

#ifndef ADS1115_hpp
#define ADS1115_hpp

#include <stdio.h>
#include "I2C.hpp"


class ADS1115
{
 
public:
    
    static const uint8_t DEFAULT_ADS1115_ADDRESS  = 0x49;
   
    
    ADS1115();
    ~ADS1115();
  
    bool begin(uint8_t deviceAddress = DEFAULT_ADS1115_ADDRESS);
    bool begin(uint8_t deviceAddress,  int &error);
    
    void stop();
    bool isOpen();
    
   
    uint8_t    getDevAddr();

    bool analogRead(uint16_t &result);
  
    /*
     Note: Set ADS1115 gain to two to read 0-20mA signals.

     When resolution is set to 16bit

     at 4mA the raw ADC value will be around 6430    
     at 20mA the raw ADC value will be around 32154.

     */
private:
   
 
    I2C         _i2cPort;
    bool        _isSetup;

};

#endif /* ADS1115_hpp */
