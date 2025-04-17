//
//  PCA9536.hpp
//  bmetest
//
//  Created by vinnie on 12/15/24.
//

#ifndef Relay16_hpp
#define Relay16_hpp
 
#include <stdio.h>
#include <vector>
#include <tuple>

#include "I2C.hpp"

using namespace std;
class PCA9536
{
 
public:
    
    typedef vector<pair<uint8_t, bool>> pinStates_t;

    PCA9536();
    ~PCA9536();
  
    //PCA9536_R11 I2C address is fixed at 0x41(65)

    bool begin(uint8_t deviceAddress = 0x41);
    bool begin(uint8_t deviceAddress,  int &error);
    void stop();
 
    bool isOpen();
    uint8_t    getDevAddr();

    bool setGPIOdirection(uint8_t ioDir);

    bool setRelayStates(pinStates_t states);
    bool getRelayStates(pinStates_t &states);
     
    bool setRelay(uint8_t relayNum, bool state);
    bool relayOn(uint8_t relayNum);
    bool relayOff(uint8_t relayNum);
    bool allOff();
    bool allOn();
   
    bool getGPIOstates(pinStates_t &states);
    bool readGPIO(uint8_t &bits);
    
    bool softReset();
    
 

private:
  
    bool        getInReg(uint8_t &bits);
    bool        setOutReg(uint8_t bits);

    I2C         _i2cPort;
    bool        _isSetup;
    uint8_t    _relayBits;
};


#endif /* Relay16_hpp */
