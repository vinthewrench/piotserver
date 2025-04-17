//
//  MCP23008.cpp
//  ncd
//
//  Created by vinnie on 4/2/25.
//

#include <stdio.h>
#include "MCP23008.hpp"
#include "LogMgr.hpp"

// https://www.microchip.com/en-us/product/mcp23008

// registers
#define MCP23008_IODIR 0x00   //!< I/O direction register
#define MCP23008_IPOL 0x01    //!< Input polarity register
#define MCP23008_GPINTEN 0x02 //!< Interrupt-on-change control register
#define MCP23008_DEFVAL  0x03 //!< Default compare register for interrupt-on-change
#define MCP23008_INTCON 0x04 //!< Interrupt control register
#define MCP23008_IOCON 0x05  //!< Configuration register
#define MCP23008_GPPU 0x06   //!< Pull-up resistor configuration register
#define MCP23008_INTF 0x07   //!< Interrupt flag register
#define MCP23008_INTCAP 0x08 //!< Interrupt capture register
#define MCP23008_GPIO 0x09   //!< Port register
#define MCP23008_OLAT 0x0A   //!< Output latch register



MCP23008::MCP23008(){
    _isSetup = false;
}

MCP23008::~MCP23008(){
    stop();
}

bool MCP23008::begin(uint8_t deviceAddress){
    int error = 0;

    return begin(deviceAddress, error);
}
 
bool MCP23008::begin(uint8_t deviceAddress,   int &error){
 
    _isSetup = _i2cPort.begin(deviceAddress, error);

//    LOGT_DEBUG("MCP23008(%02x) begin: %s", deviceAddress, _isSetup?"OK":"FAIL");
    
    if(!softReset())
        return  false;

    return _isSetup;
}
 
void MCP23008::stop(){
    if(_isSetup){
//        LOGT_DEBUG("MCP23008(%02x) stop\n",  _i2cPort.getDevAddr());

        _isSetup = false;
        _i2cPort.stop();

    }
  }
 
uint8_t    MCP23008::getDevAddr(){
    return _i2cPort.getDevAddr();
};

bool MCP23008::isOpen(){
    return _isSetup;
    
};

bool MCP23008::softReset(){
    bool success = false;
 
//    LOGT_DEBUG("MCP23008(%02x) softReset\n",  _i2cPort.getDevAddr());

    uint8_t regs[10]  = {0};
    regs[0] =  0xFF;  //all inputs
  
    success = _i2cPort.writeBytes(MCP23008_IODIR, sizeof(regs),regs);

    if(!success){
        LOGT_ERROR("MCP23008(%02X) softReset failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }
 
    return success;
}

bool MCP23008::setGPIOdirection(uint8_t ioDir){
    bool success = false;
 
 //   LOGT_DEBUG("MCP23008(%02x) setGPIOdirection = %02X)\n",  _i2cPort.getDevAddr(), ioDir);

    uint8_t regs[1]  = {0};
    regs[0] =  ioDir;  // io Direction
  
    success = _i2cPort.writeBytes(MCP23008_IODIR, sizeof(regs),regs);

    if(!success){
        LOGT_ERROR("MCP23008(%02X) setGPIOdirection failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }
    
    // set the pullups to match
    regs[0] =  ioDir;  // io Direction

    success = _i2cPort.writeBytes(MCP23008_GPPU, sizeof(regs),regs);

    if(!success){
        LOGT_ERROR("MCP23008(%02X) setGPIO PULLUP failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
  }

   
    return success;
}



bool MCP23008::allOff()
{
    uint8_t bits = 0;
    return  setOLAT(bits);
}

bool MCP23008::allOn()
{
    uint8_t bits = 0xFF;
    return  setOLAT(bits);
}

bool MCP23008::setRelay(uint8_t relayNum, bool state){

    bool success = false;
 
    if (relayNum > 7)
        return false;

    uint8_t bits;
    
    success = getOLAT(bits);
    if(success) {
        
        if(state)
            bits |= 1<<(relayNum);
        else
            bits &= ~(1<<(relayNum));

          success = setOLAT(bits);
     }
    return success;
}


bool MCP23008::setRelayStates(pinStates_t states){
    bool success = false;

    uint8_t bits;
    
    success = getOLAT(bits);
    if(success) {
        for(auto s : states){
            uint8_t relayNum    = s.first;
            bool state          = s.second;
         
            if(state)
                bits |= 1<<(relayNum);
            else
                bits &= ~(1<<(relayNum));
         }
        success = setOLAT(bits);
   }
  return success;
}

bool MCP23008::relayOn(uint8_t relayNum)
{
    bool success = setRelay(relayNum,true);
    return success;
 }

bool MCP23008::relayOff(uint8_t relayNum)
{
    bool success = setRelay(relayNum,false);
    return success;
}


bool MCP23008::getRelayStates(pinStates_t &statesOut){
    
    bool success = false;
    
    uint8_t bits;
    
    success = getOLAT(bits);
    if(success) {
        
        pinStates_t states; states.clear();
         
        for(uint8_t relayNum = 0; relayNum < 8; relayNum++, bits=bits>>1){
            bool state = bits&1;
            states.push_back(make_pair(relayNum,state));
        }
        statesOut = states;
        
    }
    
    return success;
}


bool MCP23008::readGPIO(uint8_t &bitsOut){
 
   
    bool success = false;
    
    uint8_t bits;
    
    success = _i2cPort.readByte(MCP23008_GPIO, bits);
    
    if(!success){
        LOGT_ERROR("MCP23008(%02X) read MCP23008_GPIO register failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }

    bitsOut = bits;
    
    return true;
}

bool MCP23008::getGPIOstates(pinStates_t &statesOut){
    
    bool success = false;
    uint8_t gpioBits;

    // assume that we can't read the bits from device, so we use the _relayBits
    success = _i2cPort.readByte(MCP23008_GPIO, gpioBits);
    
    if(!success){
        LOGT_ERROR("MCP23008(%02X) read MCP23008_OLAT register failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }
  
    pinStates_t states; states.clear();
    uint8_t bits =  gpioBits;

    for(uint8_t relayNum = 0; relayNum < 8; relayNum++, bits=bits>>1){
        bool state = bits&1;
        states.push_back(make_pair(relayNum,state));
    }
    statesOut = states;
    
    return success;
}
 
bool   MCP23008::getOLAT(uint8_t &bits){
  
    bool success = false;
    
 //   LOGT_DEBUG("MCP23008(%02x) getOLAT\n",  _i2cPort.getDevAddr());

    success = _i2cPort.readByte(MCP23008_OLAT, bits);
    
    if(!success){
        LOGT_ERROR("MCP23008(%02X) read MCP23008_OLAT register failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }
    return success;
}

bool    MCP23008::setOLAT(uint8_t bits){
    bool success = false;
 
//    LOGT_DEBUG("MCP23008(%02x) setOLAT\n",  _i2cPort.getDevAddr());
      
    success = _i2cPort.writeByte(MCP23008_OLAT, bits);

    if(!success){
        LOGT_ERROR("MCP23008(%02X) set MCP23008_OLAT register failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }
 
    return success;
}

