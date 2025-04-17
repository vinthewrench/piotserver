//
//  SHT25.cpp
//  SHT25
//
//  Created by vinnie on 3/12/25.
//

#include "SHT25.hpp"
#include <stdio.h>
#include <cstring>            //Needed for memset and string functions
#include "LogMgr.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <thread>

static const uint8_t SOFT_RESET  = 0XFE; // command soft reset
static const uint8_t TRIGGER_RH_MEASUREMENT_NHM  = 0xF5;  // command trig. hum. meas. no hold master
static const uint8_t TRIGGER_T_MEASUREMENT_NHM     = 0xF3;  // command trig. temp meas. no hold master

/*
 https://sensirion.com/products/catalog/SHT25
 */

// Generator polynomial for CRC
const uint16_t POLYNOMIAL = 0x131;  // P(x)=x^8+x^5+x^4+1 = 100110001
static uint8_t SHT2X_CalcCrc(uint8_t data[], uint8_t nbrOfBytes)
{
    uint8_t bit;        // bit mask
    uint8_t crc = 0; // calculated checksum
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

//static uint8_t SHT2x_CheckCrc(uint8_t data[], uint8_t nbrOfBytes, uint8_t checksum)
////==============================================================================
//{
//    uint8_t crc = 0;
//    uint8_t byteCtr;
//  //calculates 8-Bit checksum with given polynomial
//  for (byteCtr = 0; byteCtr < nbrOfBytes; ++byteCtr)
//  { crc ^= (data[byteCtr]);
//    for (uint8_t bit = 8; bit > 0; --bit)
//    { if (crc & 0x80) crc = (crc << 1) ^ POLYNOMIAL;
//      else crc = (crc << 1);
//    }
//  }
//  if (crc != checksum) return -1;
//  else return 0;
//}


SHT25::SHT25(){
    _isSetup = false;
}

SHT25::~SHT25(){
    stop();
}


bool SHT25::begin(uint8_t deviceAddress){
    int error = 0;

    return begin(deviceAddress, error);
}
 

bool SHT25::begin(uint8_t deviceAddress,    int &error){
   
//    LOGT_DEBUG("SHT25(%02X) begin ",deviceAddress);
 
    if(!_i2cPort.begin(deviceAddress, error))
        return  false;
    
    if(!softReset())
        return  false;
 
    _isSetup = true;

    return _isSetup;
}
 

bool SHT25::softReset(){
    
//    LOGT_DEBUG("SHT25(%02x) softReset",  _i2cPort.getDevAddr());

    uint8_t cmd[1] = {0};
    cmd[0] = SOFT_RESET;
   
    if(!_i2cPort.stdWriteBytes(1,cmd)){
        LOGT_ERROR("SHT25(%02X) WRITE failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }

    std::this_thread::sleep_for(std::chrono::microseconds(15000));

    return true;
}

void SHT25::stop(){
    
    if(_isSetup){
//        LOGT_DEBUG("SHT25(%02x) stop",  _i2cPort.getDevAddr());

       _isSetup = false;
       _i2cPort.stop();

    }
  }
 
bool SHT25::isOpen(){
return _i2cPort.isAvailable() && _isSetup;
};



uint8_t    SHT25::getDevAddr(){
    return _i2cPort.getDevAddr();
};


bool SHT25::readSerialNumber(uint8_t serialNo[8]){

    uint8_t block[6] = {0};
    uint8_t cmd[2] = {0};

  //  LOGT_DEBUG("SHT25(%02x) readSerialNumber",  _i2cPort.getDevAddr());

   if(!_i2cPort.isAvailable()) return false;
   
   cmd[0] = 0xFA;    // CMD_READ_SERIALNBR
   cmd[1] = 0x0F;
   
    if(!_i2cPort.stdWriteBytes(2,cmd)){
       LOGT_ERROR("SHT25(%02X) WRITE readSerialNumber failed: %s",
                  _i2cPort.getDevAddr(),  strerror(errno));
       return false;
   }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

   if(!_i2cPort.stdReadBytes(4,block)){
       LOGT_ERROR("SHT25(%02X) Read failed: %s",
                  _i2cPort.getDevAddr(),  strerror(errno));
       return false;
       
   }
    
    serialNo[5] = block[0];
    serialNo[4] = block[1];
    serialNo[3] = block[2];
    serialNo[2] = block[3];

    cmd[0] = 0xFC;    // CMD_READ_SERIALNBR
    cmd[1] = 0xC9;
    
     if(!_i2cPort.stdWriteBytes(2,cmd)){
        LOGT_ERROR("SHT25(%02X) WRITE readSerialNumber failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }
  
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    if(!_i2cPort.stdReadBytes(4,block)){
        LOGT_ERROR("SHT25(%02X) Read failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }

    serialNo[1] = block[0];
    serialNo[0] = block[1];
    serialNo[7] = block[2];
    serialNo[6] = block[3];
 
   return true;
}
 
#define  CONCAT_BYTES(msb, lsb)             (((uint16_t)msb << 8) | (uint16_t)lsb)
 
bool SHT25::readSensor(SHT25_data &data){
  
    uint8_t cmd[1] = {0};
    uint8_t block[3] = {0};

//    LOGT_DEBUG("SHT25(%02x) readSensor",  _i2cPort.getDevAddr());
    
    if(!_i2cPort.isAvailable()) return false;
    
//    LOGT_DEBUG("SHT25(%02x) Read Humidity",  _i2cPort.getDevAddr());
    
     cmd[0] = TRIGGER_RH_MEASUREMENT_NHM;
    
      if(!_i2cPort.stdWriteBytes(1,cmd)){
        LOGT_ERROR("SHT25(%02X) WRITE failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
 
     if(!_i2cPort.stdReadBytes(3,block)){
        LOGT_ERROR("SHT25(%02X) Read failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }
    
    if(block[2] !=  SHT2X_CalcCrc(&block[0],2)){
        LOGT_ERROR("SHT25(%02X) CRC Error on read: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }
    
    // Convert the data
    //// RH= -6 + 125 * SRH/2^16
    data.humidity = (125.0 * (block[0] * 256 + block[1]) / 65535.0);// - 6;
 
    cmd[0] = TRIGGER_T_MEASUREMENT_NHM;
    
     if(!_i2cPort.stdWriteBytes(1,cmd)){
        LOGT_ERROR("SHT25(%02X) WRITE failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }
    
     std::this_thread::sleep_for(std::chrono::milliseconds(300));
 
    if(!_i2cPort.stdReadBytes(3,block)){
        LOGT_ERROR("SHT25(%02X) Read failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }
 
    if(block[2] !=  SHT2X_CalcCrc(&block[0],2)){
        LOGT_ERROR("SHT25(%02X) CRC Error on read: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }

    uint16_t t = CONCAT_BYTES(block[0], block[1]);
    //T= -46.85 + 175.72 * ST/2^16
    data.temperature = -46.85 + (175.72 * t / 65535.0);
    
    return true;
}

//
//
// #include <sys/ioctl.h>                                                  // Serial Port IO Controls
//
//void SHT25::test()
//{
//  
//    uint8_t addr = 0x40;
//    
//    static const char *bus = "/dev/i2c-1";
//#define I2C_SLAVE    0x0703
//
//    printf("%02x --- test SHT25 ---\n",addr);
//
//    // Create I2C bus
//    int file;
//    if ((file = open(bus, O_RDWR)) < 0)
//    {
//        printf("Failed to open the bus. \n");
//        exit(1);
//    }
//    // Get I2C device, SHT25 I2C address is 0x40(64)
//    ioctl(file, I2C_SLAVE, addr);
//
//    // Send humidity measurement command, NO HOLD master
//    unsigned char config[1] = {0};
//    config[0] = 0xF5;
//    write(file, config, 1);
//    std::this_thread::sleep_for(std::chrono::milliseconds(300));
//
//    // Read 2 bytes of data
//    // humidity msb, humidity lsb
//    char data[2] = {0};
//    if(read(file, data, 2) != 2)
//    {
//        printf("Erorr : Input/output Erorr \n");
//    }
//    else
//    {
//        // Convert the data
//        float humidity = (((data[0] * 256.0 + data[1]) * 125.0) / 65536.0) - 6;
//        
//        //Output data to screen
//        printf("\tRelative Humidity : %.2f RH \n", humidity);
//    }
//    
//    // Send temperature measurement command, NO HOLD master
//    config[0] = 0xF3;
//    write(file, config, 1);
//    std::this_thread::sleep_for(std::chrono::milliseconds(300));
//
//    // Read 2 bytes of data
//    // temp msb, temp lsb
//    if(read(file, data, 2) != 2)
//    {
//        printf("Erorr : Input/output Erorr \n");
//    }
//    else
//    {
//        // Convert the data
//        float cTemp = (((((data[0] & 0xFF) * 256) + (data[1] & 0xFF)) * 175.72) / 65536.0) - 46.85;
//        float fTemp = (cTemp * 1.8) + 32;
//        
//        //Output data to screen
//        printf("\tTemperature in Celsius : %.2f C \n", cTemp);
//        printf("\tTemperature in Fahrenheit : %.2f F \n", fTemp);
//    }
//    
//    printf("\n");
//    
//}
