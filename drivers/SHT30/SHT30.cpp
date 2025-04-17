//
//  SHT30.cpp
//  sht30
//
//  Created by vinnie on 3/12/25.
//

#include "SHT30.hpp"
#include <stdio.h>
#include <cstring>            //Needed for memset and string functions
#include "LogMgr.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <thread>

#define SHT30_DEFAULT_ADDR 0x44


/*
 https://sensirion.com/products/catalog/SHT30-DIS-F
 */


SHT30::SHT30(){
    _isSetup = false;
}

SHT30::~SHT30(){
    stop();
    
}


bool SHT30::begin(uint8_t deviceAddress){
    int error = 0;

    return begin(deviceAddress, error);
}
 

bool SHT30::begin(uint8_t deviceAddress,    int &error){
    
    if(!_i2cPort.begin(deviceAddress, error))
        return  false;
  
    if(!softReset())
        return  false;
 
    _isSetup = true;
    
    return _isSetup;
}
 
void SHT30::stop(){
//    LOGT_INFO("SHT30(%02x) stop\n",  _i2cPort.getDevAddr());

    _isSetup = false;
    _i2cPort.stop();
}
 
bool SHT30::isOpen(){
return _i2cPort.isAvailable() && _isSetup;
};



uint8_t    SHT30::getDevAddr(){
    return _i2cPort.getDevAddr();
};


bool SHT30::softReset(){
    
//    LOGT_DEBUG("SHT30(%02x) softReset",  _i2cPort.getDevAddr());
 
    uint8_t cmd[2] = {0};
    cmd[0] = 0x30;
    cmd[1] = 0xA2;   //Reset command

    if(!_i2cPort.stdWriteBytes(2,cmd)){
        LOGT_ERROR("SHT30(%02X) WRITE failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(7));
    
    return true;
}


// Generator polynomial for CRC
#define POLYNOMIAL  0x131 // P(x) = x^8 + x^5 + x^4 + 1 = 100110001
static uint8_t SHT3X_CalcCrc(uint8_t data[], uint8_t nbrOfBytes)
{
    uint8_t bit;        // bit mask
    uint8_t crc = 0xFF; // calculated checksum
    uint8_t byteCtr;    // byte counter
  
  // calculates 8-Bit checksum with given polynomial
  for(byteCtr = 0; byteCtr < nbrOfBytes; byteCtr++)
  {
    crc ^= (data[byteCtr]);
    for(bit = 8; bit > 0; --bit)
    {
      if(crc & 0x80) crc = (crc << 1) ^ POLYNOMIAL;
      else           crc = (crc << 1);
    }
  }
  
  return crc;
}


#define  CONCAT_BYTES(msb, lsb)             (((uint16_t)msb << 8) | (uint16_t)lsb)


#define SHT3X_ACCURACY_HIGH 0x2400
#define SHT3X_ACCURACY_MEDIUM 0x240b
#define SHT3X_ACCURACY_LOW 0x2416

#define SHT3X_ACCURACY_HIGH_DURATION 15
#define SHT3X_ACCURACY_MEDIUM_DURATION 6
#define SHT3X_ACCURACY_LOW_DURATION 4

#define SHT3X_RESET_STATUS 0x3041
#define SHT3X_READ_STATUS 0xF32D

bool SHT30::readSensor(SHT30_data &data){
    
    if(!_i2cPort.isAvailable()) return false;
    
    uint8_t cmd[2] = {0};
    cmd[0] = 0x24;    // Clock stretching disabled
    cmd[1] = 0x0B;   // Medium repeatability measurement(
 
    if(!_i2cPort.stdWriteBytes(2,cmd)){
        LOGT_ERROR("SHT30(%02X) WRITE failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(7));
    
    uint8_t block[6] = {0};
    /*
     Temp msb,
     Temp lsb,
     Temp CRC,
     Humididty msb,
     Humidity lsb,
     Humidity CRC
     */
    
    if(!_i2cPort.stdReadBytes(6,block)){
        LOGT_ERROR("SHT30(%02X) Read failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
        
    }
    
    if( (block[2] !=  SHT3X_CalcCrc(&block[0],2))
       || (block[5] !=  SHT3X_CalcCrc(&block[3],2))){
        
        LOGT_ERROR("SHT30(%02X) CRC Error on read: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }
    
    uint16_t t = CONCAT_BYTES(block[0], block[1]);
    data.temperature = -45 + (175 * t / 65535.0);
//    data.humidity = 100 * (block[3] * 256 + block[4]) / 65535.0
    
    data.humidity = (100.0 * (block[3] * 256 + block[4]) / 65535.0);

    return true;
}



bool SHT30::readSerialNumber(uint8_t serialNo[8]){
    
    if(!_i2cPort.isAvailable()) return false;
    
    uint8_t cmd[2] = {0};
    cmd[0] = 0x37;    // CMD_READ_SERIALNBR
    cmd[1] = 0x80;
    
    if(!_i2cPort.stdWriteBytes(2,cmd)){
        LOGT_ERROR("SHT30(%02X) WRITE failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(7));
    
    uint8_t block[6] = {0};
    /*
     The Get Serial Number command returns 2 words, every word is followed by a
     CRC Checksum. Together the 2 words  (SNB_3 to SNB_0 ,
     SNB_0 is the LSB, whereas SNB_3 is the  MSB) constitute a unique serial
     number with a length of 32 bit. This serial number can be used to identify
     each sensor individually
     */
    
    if(!_i2cPort.stdReadBytes(6,block)){
        LOGT_ERROR("SHT30(%02X) Read failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
        
    }
    
    if( (block[2] !=  SHT3X_CalcCrc(&block[0],2))
       || (block[5] !=  SHT3X_CalcCrc(&block[3],2))){
        
        LOGT_ERROR("SHT30(%02X) CRC Error on read: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }
    
    serialNo[0] = block[0];
    serialNo[1] = block[1];
    serialNo[2] = block[3];
    serialNo[3] = block[4];
    serialNo[4] = 0;
    serialNo[5] = 0;
    
    return true;
}
