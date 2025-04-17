//
//  ADS1115.cpp
//  ncd
//
//  Created by vinnie on 4/2/25.
//

#include "ADS1115.hpp"
#include "LogMgr.hpp"
#include <chrono>
#include <thread>

/*=========================================================================
 POINTER REGISTER
 =========================================================================*/

#define ADS1115_POINTER_MASK        (0x03)
#define ADS1115_POINTER_CONVERSION  (0x00)
#define ADS1115_POINTER_CONFIG      (0x01)
#define ADS1115_POINTER_THRESH_LOW  (0x02)
#define ADS1115_POINTER_THRESH_HI   (0x03)

/*=========================================================================
 CONFIG REGISTER
 -----------------------------------------------------------------------*/
#define CONFIG_REG_OS_SINGLE        (0x8000)
#define CONFIG_REG_OS_BUSY          (0x0000)
#define CONFIG_REG_OS_NOTBUSY       (0x8000)

#define CONFIG_REG_MUX_MASK         (0x7000)
#define CONFIG_REG_MUX_DIFF_0_1        (0x0000) // default
#define CONFIG_REG_MUX_DIFF_0_3        (0x1000)
#define CONFIG_REG_MUX_DIFF_1_3        (0x2000)
#define CONFIG_REG_MUX_DIFF_2_3        (0x3000)
#define CONFIG_REG_MUX_CHAN_0         (0x4000)
#define CONFIG_REG_MUX_CHAN_1         (0x5000)
#define CONFIG_REG_MUX_CHAN_2         (0x6000)
#define CONFIG_REG_MUX_CHAN_3         (0x7000)

#define CONFIG_REG_PGA_6_144V       (0x0000) // +/-6.144V range
#define CONFIG_REG_PGA_4_096V       (0x0200) // +/-4.096V range
#define CONFIG_REG_PGA_2_048V       (0x0400) // +/-2.048V range // default
#define CONFIG_REG_PGA_1_024V       (0x0600) // +/-1.024V range
#define CONFIG_REG_PGA_0_512V       (0x0800) // +/-0.512V range
#define CONFIG_REG_PGA_0_256V       (0x0A00) // +/-0.256V range

#define CONFIG_REG_MODE_CONTIN        (0x0000)
#define CONFIG_REG_MODE_SINGLE        (0x0100) // default

#define CONFIG_REG_DR_8SPS            (0x0000)
#define CONFIG_REG_DR_16SPS            (0x0020)
#define CONFIG_REG_DR_32SPS            (0x0040)
#define CONFIG_REG_DR_64SPS            (0x0060)
#define CONFIG_REG_DR_128SPS        (0x0080) // default
#define CONFIG_REG_DR_250SPS        (0x00A0)
#define CONFIG_REG_DR_475SPS        (0x00C0)
#define CONFIG_REG_DR_860SPS        (0x00E0)

#define CONFIG_REG_CMODE_TRAD        (0x0000) // default
#define CONFIG_REG_CMODE_WINDOW        (0x0010)

#define CONFIG_REG_CPOL_ACTIV_LOW    (0x0000) // default
#define CONFIG_REG_CPOL_ACTIV_HIGH    (0x0080)

#define CONFIG_REG_CLATCH_NONLATCH    (0x0000) // default
#define CONFIG_REG_CLATCH_LATCH        (0x0040)

#define CONFIG_REG_CQUE_1CONV        (0x0000)
#define CONFIG_REG_CQUE_2CONV        (0x0001)
#define CONFIG_REG_CQUE_4CONV        (0x0002)
#define CONFIG_REG_CQUE_NONE        (0x0003) // default

#define ADS1115_DEFAULT_CONFIG_REG  (0x8583)    // Config register value after reset


ADS1115::ADS1115(){
    _isSetup = false;
}

ADS1115::~ADS1115(){
    stop();
    
}


bool ADS1115::begin(uint8_t deviceAddress){
    int error = 0;
    
    return begin(deviceAddress, error);
}


bool ADS1115::begin(uint8_t deviceAddress,    int &error){
    
    if(!_i2cPort.begin(deviceAddress, error))
        return  false;
    
//    LOGT_DEBUG("ADS1115(%02x) begin: %s", deviceAddress, _isSetup?"OK":"FAIL");
    
    _isSetup = true;
    
    return _isSetup;
}

void ADS1115::stop(){
    
    if(_isSetup){
 //       LOGT_DEBUG("ADS1115(%02x) stop",  _i2cPort.getDevAddr());
        
        _isSetup = false;
        _i2cPort.stop();
        
    }
}

bool ADS1115::isOpen(){
    return _isSetup;
    
};

uint8_t    ADS1115::getDevAddr(){
    return _i2cPort.getDevAddr();
};

bool ADS1115::analogRead(uint16_t &result){
    
    bool success = false;
    
    if(!_isSetup){
        return false;
    }
    
//    LOGT_DEBUG("ADS1115(%02x) analogRead",  _i2cPort.getDevAddr());
    
    
    /*
     Note: Set ADS1115 gain to two to read 0-20mA signals.
     
     When resolution is set to 16bit
     
     at 4mA the raw ADC value will be around 6430
     at 20mA the raw ADC value will be around 32154.
     
     4.5ma = 7500
     21.5ma = 32767
     
     old pumphouse values
     prop-tank-empty,7500
     prop-tank-full,15000
     
     config = c5 83
     Digital Value of Analog Input: 6790
     
     */
    
    uint8_t config[3] = {0};
    
    uint16_t cfg =
    CONFIG_REG_OS_SINGLE          |
    CONFIG_REG_MUX_CHAN_0         |
    CONFIG_REG_MODE_SINGLE        |
    CONFIG_REG_PGA_2_048V         |
    CONFIG_REG_DR_128SPS          |
    CONFIG_REG_CQUE_NONE          |
    CONFIG_REG_CMODE_TRAD         |
    CONFIG_REG_MODE_CONTIN       ;
    
    config[0] = ADS1115_POINTER_CONFIG;
    config[1] = cfg >> 8;
    config[2] = cfg & 0xFF;
    
    //    printf("config = %02x %02x\n", config[1], config[2]);
    
    success = _i2cPort.stdWriteBytes(3 ,config);
    if(!success){
        LOGT_ERROR("ADS1115(%02X) set config register failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    
#if 1
    uint8_t data[2] = {0};
    uint8_t reg[1] = {0x00};
    //      write(_i2cPort.getFD(), reg, 1);
    _i2cPort.stdWriteBytes(1,reg);
    
    success = _i2cPort.stdReadBytes( 2, data);
    
#else
    uint8_t data[2] = {0};
    success = _i2cPort.readBytes(ADS1115_POINTER_CONVERSION, 2, data);
    
#endif
    if(!success){
        LOGT_ERROR("ADS1115(%02X) read data register failed: %s",
                   _i2cPort.getDevAddr(),  strerror(errno));
        return false;
    }
    
    
    // Convert the data
    int raw_adc = (data[0] * 256 + data[1]);
    if (raw_adc > 32767)
    {
        raw_adc -= 65535;
    }
    
    result = raw_adc;
    
    return success;
}

