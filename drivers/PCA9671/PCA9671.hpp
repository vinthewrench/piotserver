//
//  PCA9671.hpp
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

class PCA9671
{
 
public:
    
    typedef vector<pair<uint8_t, bool>> pinStates_t;

    PCA9671();
    ~PCA9671();
 
    // Address of PCA9671  (20,22,24,26,28,2A,2C,2E)
 
    bool begin(uint8_t deviceAddress = 0x20);
    bool begin(uint8_t deviceAddress,  int &error);
    void stop();
 
    bool isOpen();
    uint8_t    getDevAddr();

    bool setRelayStates(pinStates_t states);
    bool getRelayStates(pinStates_t &states);
  
    bool setRelay(uint8_t relayNum, bool state);
    bool relayOn(uint8_t relayNum);
    bool relayOff(uint8_t relayNum);
    bool allOff();
    bool allOn();
    uint16_t currentState(){ return _relayBits; };
    
    bool readState(uint16_t &relays);
    
private:
 
    bool refresh();

    I2C         _i2cPort;
    bool        _isSetup;
    uint16_t    _relayBits;
};


#endif /* Relay16_hpp */
