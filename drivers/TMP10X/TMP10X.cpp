//
//  TMP10X.cpp
//  pumphouse
//
//  Created by Vincent Moscaritolo on 9/10/21.
//

#include "TMP10X.hpp"
#include "LogMgr.hpp"
#include <chrono>
#include <thread>


#define TEMPERATURE_REGISTER 0x00
#define CONFIG_REGISTER 0x01
#define T_LOW_REGISTER 0x02
#define T_HIGH_REGISTER 0x03

#if defined(__APPLE__)
#define FAKEIT 1
#endif

TMP10X::TMP10X(){
    _isSetup = false;
}

TMP10X::~TMP10X(){
    stop();
    
}

bool TMP10X::begin(uint8_t deviceAddress){
    int error = 0;

    return begin(deviceAddress, error);
}
 

bool TMP10X::begin(uint8_t deviceAddress,    int &error){
 
#if FAKEIT
    _isSetup = true;
    return true;
#endif
    
    if(!_i2cPort.begin(deviceAddress, error))
        return  false;
 
//      LOGT_INFO("TMP10X(%02x) begin\n", deviceAddress);
 
    _isSetup = true;
 
  return _isSetup;
}
 
void TMP10X::stop(){
//    LOGT_INFO("TMP10X(%02x) stop\n",  _i2cPort.getDevAddr());

    _isSetup = false;
    _i2cPort.stop();
}
 
bool TMP10X::isOpen(){
    return _isSetup;
    
};

uint8_t    TMP10X::getDevAddr(){
    return _i2cPort.getDevAddr();
};

bool TMP10X::readTempF(float& tempOut){

    bool success = false;

    float cTemp;
    
    if( _i2cPort.isAvailable()
       &&  readTempC(cTemp))
    {
        tempOut = cTemp *9.0/5.0 + 32.0;
        success = true;
    }

    return success;
};


bool TMP10X::readTempC(float& tempOut){
    
    bool success = false;
    I2C::i2c_block_t data = {0};
 
#if FAKEIT
    {
        static bool once = true;
        if(once) {
            srand((int)time(NULL));   // Initialization, should only be called once.
            once = false;
        }

        int r = (rand() % 40) - 20;
        
        tempOut =  20.4 + r;
        return true;

    }
#endif
 
    if( _i2cPort.isAvailable()
       &&  _i2cPort.readBytes(TEMPERATURE_REGISTER, 2, data))
    {
        int16_t digitalTemp;
        // Bit 0 of second byte will always be 0 in 12-bit readings and 1 in 13-bit
         // This is a special case for the TMP10X, TMP112. & TMP144 devices.
        // The Extended Mode bit (EM = 1) makes these a 13-bit device rather than 12-bit.
        // The notation is still Q4.
        if(data[1]&0x01)    // 13 bit mode
        {
            // Combine bytes to create a signed int
            digitalTemp = ((data[0]) << 5) | (data[1] >> 3);
            // Temperature data can be + or -, if it should be negative,
            // convert 13 bit to 16 bit and use the 2s compliment.
            if(digitalTemp > 0xFFF)
            {
                digitalTemp |= 0xE000;
            }
        }
        else    // 12 bit mode  Q4 Format
        {
            // Combine bytes to create a signed int
            digitalTemp = ((data[0]) << 4) | (data[1] >> 4);
            // Temperature data can be + or -, if it should be negative,
            // convert 12 bit to 16 bit and use the 2s compliment.
            if(digitalTemp > 0x7FF)
            {
                digitalTemp |= 0xF000;
            }
        }
        // Convert digital reading to analog temperature (1-bit is equal to 0.0625 C)
        
        tempOut = digitalTemp * 0.0625;
        success = true;
        
    }
    return success;
}

