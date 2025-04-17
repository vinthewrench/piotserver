//
//  TCA9534.cpp
//  pumphouse
//
//  Created by Vincent Moscaritolo on 10/29/21.
//

#include "TCA9534.hpp"
#include "LogMgr.hpp"


 
#define REGISTER_INPUT_PORT          	 	    0x00
#define REGISTER_OUTPUT_PORT          			0X01
#define REGISTER_INVERSION						0x02
#define REGISTER_CONFIGURATION        	 		0X03


TCA9534::TCA9534(){
	_isSetup = false;
}

TCA9534::~TCA9534(){
	stop();
	
}


bool TCA9534::begin(uint8_t deviceAddress){
    int error = 0;

    return begin(deviceAddress, error);
}
 

bool TCA9534::begin(uint8_t deviceAddress,    int &error){
  
	_isSetup = _i2cPort.begin(deviceAddress, error);
//     LOGT_DEBUG("TCA9534(%02x) begin: %s", deviceAddress, _isSetup?"OK":"FAIL");
 
  return _isSetup;
}
 
void TCA9534::stop(){
	_i2cPort.stop();
    _isSetup = false;
}
 
bool TCA9534::isOpen(){
	return _isSetup;
};

bool TCA9534::allOff(){
    return true;
};
 
uint8_t	TCA9534::getDevAddr(){
	return _i2cPort.getDevAddr();
};


bool TCA9534::writeConfig(uint8_t val){
	
	if(!isOpen())
			return false;

	uint8_t registerByte[2] = {REGISTER_CONFIGURATION, val};
    return  _i2cPort.writeBytes(0, 2, registerByte);
}


bool TCA9534::writeInvert(uint8_t val){
	if(!isOpen())
			return false;
	
	uint8_t registerByte[2] = {REGISTER_INVERSION, val};
    return  _i2cPort.writeBytes(0, 2, registerByte);
}

bool TCA9534::readInput(uint8_t &val){
	
	if(!isOpen())
			return false;
    
    return  _i2cPort.readByte(REGISTER_INPUT_PORT, val);

}
