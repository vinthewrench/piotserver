//
//  QWIIC_RELAY.hpp
//  Relay2
//
//  Created by vinnie on 3/16/25.
//

#ifndef QWIIC_RELAY_hpp
#define QWIIC_RELAY_hpp


#include <stdio.h>
#include "I2C.hpp"


class QWIIC_RELAY
{
    
public:
   
    typedef enum  {
        QWR_UNKNOWN = 0,
        QWR_15093 ,     //  COM-15093  single relay
        QWR_16566 ,     //  COM-16566  Quad relay
        QWR_16810 ,     //  COM-16810  DUAL SSR
    }qwr_model;
    
    QWIIC_RELAY();
    ~QWIIC_RELAY();
    
    bool begin(qwr_model model  = QWR_16810, bool alternateAddr = false);
    bool begin(qwr_model model, bool alternateAddr,  int &error);
 
    void stop();
    bool isOpen();
    
    uint8_t getDevAddr();
    qwr_model getModel() {return _model; };
    uint8_t   relayCount() {return _noRelays;};

    // relayNum  1 - 4
    bool setRelay(uint8_t relayNum, bool state);
    bool relayOn(uint8_t relayNum);
    bool relayOff(uint8_t relayNum);
    
    bool allOff();
    bool allOn();
    
   bool relayState(std::vector<bool> &state);
    

private:
  
    bool updateStatus();
    
    qwr_model   _model;
    uint8_t     _noRelays;
    bool        _relayState[4];
    
    I2C         _i2cPort;
    bool        _isSetup;
    
};
#endif /* QWIIC_RELAY_hpp */
