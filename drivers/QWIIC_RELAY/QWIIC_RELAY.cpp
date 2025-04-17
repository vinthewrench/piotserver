//
//  QWIIC_RELAY.cpp
//  Relay2
//
//  Created by vinnie on 3/16/25.
//
#include <chrono>
#include <thread>
#include <sys/errno.h>

#include "QWIIC_RELAY.hpp"
#include "LogMgr.hpp"


#define QUAD_DEFAULT_ADDRESS 0x6D
#define QUAD_ALTERNATE_ADDRESS 0x6C
#define SINGLE_DEFAULT_ADDRESS 0x18
#define SINGLE_ALTERNATE_ADDRESS 0x19

#define QUAD_SSR_DEFAULT_ADDRESS 0x08
#define QUAD_SSR_ALTERNATE_ADDRESS 0x09
#define DUAL_SSR_DEFAULT_ADDRESS 0x0A
#define DUAL_SSR_ALTERNATE_ADDRESS 0x0B

#define QUAD_CHANGE_ADDRESS 0xC7
#define SINGLE_CHANGE_ADDRESS 0x03


#define TURN_ALL_OFF 0xA
#define TURN_ALL_ON 0xB

#define RELAY_ONE_TOGGLE 0x01
#define RELAY_TWO_TOGGLE 0x02
#define RELAY_THREE_TOGGLE 0x03
#define RELAY_FOUR_TOGGLE 0x04


// Commands to request the state of the relay, whether it is currently on or
// off.
 
#define RELAY_STATUS_ONE  0x05
#define RELAY_STATUS_TWO  0x06
#define RELAY_STATUS_THREE 0x07
#define RELAY_STATUS_FOUR 0x08

/*
 COM-15093  single relay
 COM-16566  Quad relay
 COM-16810  DUAL SSR
 */

/*
 https://www.sparkfun.com/sparkfun-qwiic-dual-solid-state-relay.html
 */


QWIIC_RELAY::QWIIC_RELAY(){
    _isSetup = false;
}

QWIIC_RELAY::~QWIIC_RELAY(){
    stop();
    
}


bool QWIIC_RELAY::begin(qwr_model model, bool alternateAddr){
    int error = 0;

    return begin(model, alternateAddr, error);
}
 

bool QWIIC_RELAY::begin(qwr_model model, bool alternateAddr,  int &error){
 
    uint8_t devAddr = 0;
    
    _noRelays = 0;
    _model  = QWR_UNKNOWN;
    for(int i = 0; i < sizeof(_relayState); i++) _relayState[i] = false;
    
    switch (model) {
        case QWR_15093:  //  COM-15093  single relay
            devAddr = alternateAddr?SINGLE_ALTERNATE_ADDRESS:SINGLE_DEFAULT_ADDRESS;
            _noRelays = 1;
           break;

        case QWR_16566:  //  COM-16566  Quad relay
            devAddr = alternateAddr?QUAD_ALTERNATE_ADDRESS:QUAD_DEFAULT_ADDRESS;
            _noRelays = 4;
          break;

        case QWR_16810:  //  COM-16810  DUAL SSR
            devAddr = alternateAddr?DUAL_SSR_ALTERNATE_ADDRESS:DUAL_SSR_DEFAULT_ADDRESS;
            _noRelays = 2;
          break;

        default:
            break;
    }
    
    if(devAddr == 0) {
        LOGT_ERROR("QWIIC_RELAY model number %d unsupported", model);
        errno = EINVAL;
        error = EINVAL; // Invalid Argument.
        
        return false;
    }
    
    if(!_i2cPort.begin(devAddr, error))
        return  false;
 
    if(!_i2cPort.smbQuick()){
        error = ENXIO;
        errno = EINVAL;
        return  false;
    }
    
    _model = model;
    _isSetup = true;
    
    return _isSetup;
}
 
void QWIIC_RELAY::stop(){
//    LOGT_INFO("QWIIC_RELAY(%02x) stop\n",  _i2cPort.getDevAddr());

    _isSetup = false;
    _i2cPort.stop();
}
 
bool QWIIC_RELAY::isOpen(){
return _i2cPort.isAvailable() && _isSetup;
};



uint8_t    QWIIC_RELAY::getDevAddr(){
    return _i2cPort.getDevAddr();
};

 
bool QWIIC_RELAY::allOn(){
    bool status = false;
    
    if(_model == QWR_15093){
        status = setRelay(1, true);
    }
    else {
        uint8_t cmd[1] = {TURN_ALL_ON};
        
        status = _i2cPort.stdWriteBytes(sizeof(cmd),cmd);
        
        updateStatus();
    }
    
    if(!status){
        LOGT_ERROR("QWIIC_RELAY(%02X) ALL ON failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
    }
    
    return status;
};


bool QWIIC_RELAY::allOff(){
    bool status = false;
 
    if(_model == QWR_15093){
        status = setRelay(1, false);
    }
    else {
        uint8_t cmd[1] = {TURN_ALL_OFF};
        
        status = _i2cPort.stdWriteBytes(sizeof(cmd),cmd);
        
        updateStatus();
    }
    
    if(!status){
        LOGT_ERROR("QWIIC_RELAY(%02X) ALL OFF failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
    }
    
    return status;
};


bool QWIIC_RELAY::relayOn(uint8_t relayNum){
 
   return setRelay(relayNum, true);
 };

bool QWIIC_RELAY::relayOff(uint8_t relayNum){
    return setRelay(relayNum, false);
};


bool  QWIIC_RELAY::setRelay(uint8_t relayNum, bool state){
  
    bool status = false;
    
    if(relayNum == 0 || relayNum > _noRelays) {
        LOGT_ERROR("QWIIC_RELAY(%02X) relayOn(%d) param error",
                   _i2cPort.getDevAddr(), relayNum);
        return false;
    }
  
    if(_model == QWR_15093){
        
         #define COMMAND_RELAY_OFF             0x00
         #define COMMAND_RELAY_ON               0x01

        uint8_t cmd[1] = {0};
        cmd[0] = state?COMMAND_RELAY_ON:COMMAND_RELAY_OFF;
        
        status = _i2cPort.stdWriteBytes(sizeof(cmd),cmd);

    }
    else
    {
        updateStatus();
  
        if( _relayState[relayNum -1] == state )
            return true;
     
        /*
         
         relayNum is the same as the command
         
         #define RELAY_ONE_TOGGLE 0x01
         #define RELAY_TWO_TOGGLE 0x02
         #define RELAY_THREE_TOGGLE 0x03
         #define RELAY_FOUR_TOGGLE 0x04
         */
        uint8_t cmd[1] = {relayNum};
        status = _i2cPort.stdWriteBytes(sizeof(cmd),cmd);
      
    }
   
    if(!status){
        LOGT_ERROR("QWIIC_RELAY(%02X) WRITE failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
    }
 
    return status;
}



bool QWIIC_RELAY::relayState(std::vector<bool> &state){
    bool status = false;
    
    status = updateStatus();
  
    if(status){
        state.clear();
        
        for(int i = 0; i < _noRelays; i++)
            state.push_back(_relayState[i]);
    }
    
    return status;
}

bool QWIIC_RELAY::updateStatus(){
    
    bool status = false;
    
#if 0
    I2C::i2c_block_t block;
    status = _i2cPort.readBlock(RELAY_STATUS_ONE, _noRelays, block);
    
    if(status){
        for(int i = 0; i < _noRelays; i++){
            _relayState[i] = block[i]?true:false;
        }
        return true;
    }
    
    
#else
    uint8_t stat[4] = {0};
    status = _i2cPort.readBytes(RELAY_STATUS_ONE, _noRelays ,stat);
    if(status){
        for(int i = 0; i < _noRelays; i++){
            _relayState[i] = stat[i]?true:false;
        }
        
        return true;
    }
#endif
    
    return false;
}
