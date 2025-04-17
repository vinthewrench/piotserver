//
//  MCP23008.hpp
 //
 
#ifndef MCP23008_hpp
#define MCP23008_hpp
 
#include <stdio.h>
#include <vector>
#include <tuple>

#include "I2C.hpp"

using namespace std;

class MCP23008
{
 
public:
    
    typedef vector<pair<uint8_t, bool>> pinStates_t;

    MCP23008();
    ~MCP23008();
  
    bool begin(uint8_t deviceAddress = 0x20);
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

    bool        getOLAT(uint8_t &bits);
    bool        setOLAT(uint8_t bits);
 
    I2C         _i2cPort;
    bool        _isSetup;
};


#endif /* MCP23008_hpp */
