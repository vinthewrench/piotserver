//
//  PCA9671.cpp
//  bmetest
//
//  Created by vinnie on 12/15/24.
//

#include "PCA9671.hpp"
#include "LogMgr.hpp"


PCA9671::PCA9671(){
    _isSetup = false;
}

PCA9671::~PCA9671(){
    stop();
}

bool PCA9671::begin(uint8_t deviceAddress){
    int error = 0;

    return begin(deviceAddress, error);
}
 
bool PCA9671::begin(uint8_t deviceAddress,   int &error){
    
    _isSetup = _i2cPort.begin(deviceAddress, error);
    
//    LOGT_DEBUG("PCA9671(%02x) begin: %s", deviceAddress, _isSetup?"OK":"FAIL");
    
    allOff();
    return _isSetup;
}
 
void PCA9671::stop(){
 //   LOGT_INFO("PCA9671(%02x) stop\n",  _i2cPort.getDevAddr());

    _isSetup = false;
    _i2cPort.stop();

}
 
uint8_t    PCA9671::getDevAddr(){
    return _i2cPort.getDevAddr();
};

bool PCA9671::isOpen(){
    return _isSetup;
    
};

bool PCA9671::allOff()
{
    _relayBits = 0;
    return refresh();
}

bool PCA9671::allOn()
{
    _relayBits = 0xFFFF;
    return refresh();
}

bool PCA9671::setRelay(uint8_t relayNum, bool state){
    if(state)
        _relayBits |= 1<<(relayNum-1);
    else
        _relayBits &= ~(1<<(relayNum-1));
    return refresh();
}


bool PCA9671::setRelayStates(pinStates_t states){
  
    for(auto s : states){
        uint8_t relayNum    = s.first;
        bool state          = s.second;
     
        if(state)
            _relayBits |= 1<<(relayNum-1);
        else
            _relayBits &= ~(1<<(relayNum-1));
     }
    return refresh();
}

bool PCA9671::relayOn(uint8_t relayNum)
{
    if (relayNum < 1 || relayNum > 16)
        return false;
    _relayBits |= 1<<(relayNum-1);
    return refresh();

}

bool PCA9671::relayOff(uint8_t relayNum)
{
    if (relayNum < 1 || relayNum > 16)
        return false;
    _relayBits &= ~(1<<(relayNum-1));
    return refresh();
}

bool PCA9671::refresh()
{
    uint8_t data[2] = { static_cast<uint8_t>(~(_relayBits & 0xFF)),
    static_cast<uint8_t>(~((_relayBits >> 8) & 0xFF)) };
    bool success =  _i2cPort.writeBytes(0, 2, data);
    return success;
}


bool PCA9671::readState(uint16_t &relays){
 
    // assume that we can't read the bits from device, so we use the _relayBits
    relays = _relayBits;
    return true;
}

bool PCA9671::getRelayStates(pinStates_t &states){

    // assume that we can't read the bits from device, so we use the _relayBits
        uint16_t bits =  _relayBits;
        for(uint8_t relayNum = 1; relayNum < 17; relayNum++, bits=bits>>1){
            bool state = bits&1;
            states.push_back(make_pair(relayNum,state));
        }
    return true;
}
