//
//  PCA9536.cpp
 //

#include "PCA9536.hpp"
#include "LogMgr.hpp"


#define PCA9536_IN     0x00       //!< inut port register
#define PCA9536_OUT     0x01       //!< output port register
#define PCA9536_POLARITY  0x02       //!< polarity register
#define PCA9536_CONFIG  0x03   //!< I/O direction register
 
//!
//!
PCA9536::PCA9536(){
    _isSetup = false;
}

PCA9536::~PCA9536(){
    stop();
}

bool PCA9536::begin(uint8_t deviceAddress){
    int error = 0;

    return begin(deviceAddress, error);
}
 
bool PCA9536::begin(uint8_t deviceAddress,   int &error){
    
    _isSetup = _i2cPort.begin(deviceAddress, error);
    
 //   LOGT_DEBUG("PCA9536(%02x) begin: %s", deviceAddress, _isSetup?"OK":"FAIL");
       
    if(!softReset())
        return  false;

    return _isSetup;
}
 
void PCA9536::stop(){
    
    if(_isSetup){
        LOGT_DEBUG("PCA9536(%02x) stop",  _i2cPort.getDevAddr());
        
        _isSetup = false;
        _i2cPort.stop();
        
    }
}
 
uint8_t    PCA9536::getDevAddr(){
    return _i2cPort.getDevAddr();
};

bool PCA9536::isOpen(){
    return _isSetup;
    
};

bool PCA9536::softReset(){
    bool success = false;
 
//    LOGT_DEBUG("PCA9536(%02x) softReset",  _i2cPort.getDevAddr());

    success = setGPIOdirection(0x0F);  //all inputs

     if(!success){
        LOGT_ERROR("PCA9536(%02X) softReset failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }
 
    return success;
}


bool PCA9536::setGPIOdirection(uint8_t ioDir){
    bool success = false;
 
 //   LOGT_DEBUG("PCA9536(%02x) setGPIOdirection = %02X",  _i2cPort.getDevAddr(), ioDir);

    success = _i2cPort.writeByte(PCA9536_CONFIG, ioDir);

    if(!success){
        LOGT_ERROR("PCA9536(%02X) setGPIOdirection failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
    }
 
    return success;
}


bool PCA9536::allOff()
{
    _relayBits = 0;
    return setOutReg(_relayBits);
}

bool PCA9536::allOn()
{
    _relayBits = 0x0F;
    return setOutReg(_relayBits);
}

bool PCA9536::setRelay(uint8_t relayNum, bool state){
    
    if (relayNum > 3)
        return false;

    if(state)
        _relayBits |= 1<<(relayNum);
    else
        _relayBits &= ~(1<<(relayNum));
    
    return setOutReg(_relayBits);
}

bool PCA9536::setRelayStates(pinStates_t states){
    bool success = false;

    uint8_t bits;
    
    success = getInReg(bits);
    if(success) {
        for(auto s : states){
            
            uint8_t relayNum    = s.first;
            bool state          = s.second;
            if(relayNum < 4){
                if(state)
                    bits |= 1<<(relayNum);
                else
                    bits &= ~(1<<(relayNum));

            }
           }
        success = setOutReg(bits);
   }
  return success;
}


bool PCA9536::getRelayStates(pinStates_t &statesOut) {
    return getGPIOstates(statesOut);
}

bool PCA9536:: getGPIOstates(pinStates_t &statesOut){
    
    bool success = false;
    
    uint8_t bits;
    
    success = getInReg(bits);
    if(success) {
        
        pinStates_t states; states.clear();
         
        for(uint8_t relayNum = 0; relayNum < 4; relayNum++, bits=bits>>1){
            bool state = bits&1;
            states.push_back(make_pair(relayNum,state));
        }
        statesOut = states;
        
    }
    
    return success;
}


bool PCA9536::relayOn(uint8_t relayNum)
{
    bool success = setRelay(relayNum,true);
    return success;
}

bool PCA9536::relayOff(uint8_t relayNum)
{
    bool success = setRelay(relayNum,false);
    return success;
}
 

bool PCA9536::getInReg(uint8_t &bitsOut){
    bool success = false;

    uint8_t bits;
    success = _i2cPort.readByte(PCA9536_IN, bits);
    
    if(!success){
        LOGT_ERROR("PCA9536(%02X) getInReg failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }
    bitsOut = bits & 0x0f;  // only 4 bits
    
    return success;
}

bool PCA9536::setOutReg(uint8_t bits){
    
    bool success = false;
    
    success = _i2cPort.writeByte(PCA9536_OUT, bits);
    
    if(!success){
        LOGT_ERROR("PCA9536(%02X) setOutReg failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }
    
    return success;
}
