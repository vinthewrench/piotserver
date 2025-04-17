//
//  VELM6030.cpp
//  pIoTServer
//
//  Created by vinnie on 1/21/25.
//

#include "VELM6030.hpp"
#include "LogMgr.hpp"

#include <chrono>
#include <thread>
#include <cmath>

using namespace std::chrono_literals;


#define ENABLE        0x01
#define DISABLE       0x00
#define SHUTDOWN      0x01
#define POWER         0x00
#define NO_INT        0x00
#define INT_HIGH      0x01
#define INT_LOW       0x02
#define UNKNOWN_ERROR 0xFF


enum VEML6030_16BIT_REGISTERS {

  SETTING_REG            = 0x00,
  H_THRESH_REG,
  L_THRESH_REG,
  POWER_SAVE_REG,
  AMBIENT_LIGHT_DATA_REG,
  WHITE_LIGHT_DATA_REG,
  INTERRUPT_REG

};

enum VEML6030_16BIT_REG_MASKS {
 
  THRESH_MASK            = 0x0,
  GAIN_MASK              = 0xE7FF,
  INTEG_MASK             = 0xFC3F,
  PERS_PROT_MASK         = 0xFFCF,
  INT_EN_MASK            = 0xFFFD,
  SD_MASK                = 0xFFFE,
  POW_SAVE_EN_MASK       = 0x06, // Most of this register is reserved
  POW_SAVE_MASK          = 0x01, // Most of this register is reserved
  INT_MASK               = 0xC000
  
};

enum REGISTER_BIT_POSITIONS {

  NO_SHIFT               = 0x00,
  INT_EN_POS             = 0x01,
  PSM_POS                = 0x01,
  PERS_PROT_POS          = 0x04,
  INTEG_POS              = 0x06,
  GAIN_POS               = 0xB,
  INT_POS                = 0xE

};



VELM6030::VELM6030(){
    _isSetup = false;
}

VELM6030::~VELM6030(){
    stop();
    
}


bool VELM6030::begin(uint8_t deviceAddress){
    int error = 0;

    return begin(deviceAddress, error);
}
 

bool VELM6030::begin(uint8_t deviceAddress,    int &error){
    
    // Possible integration times in milliseconds: 800, 400, 200, 100, 50, 25
    // Higher times give higher resolutions and should be used in darker light.
    int intTime = 100;
    
    // Possible values: .125, .25, 1, 2
    // Both .125 and .25 should be used in most cases except darker rooms.
    // A gain of 2 should only be used if the sensor will be covered by a dark
    // glass.
    float gain = .125;
    
    
    if(!_i2cPort.begin(deviceAddress, error))
        return  false;
    
 //   LOGT_INFO("VELM6030(%02x) begin", deviceAddress);
    
    // Device is powered down by default.
    if( powerOn()
       && setGain(gain)
       && setIntegTime(intTime)
       ){
        _isSetup = true;
    }
    
    return _isSetup;
}
 
void VELM6030::stop(){
    LOGT_INFO("VELM6030(%02x) stop\n",  _i2cPort.getDevAddr());

    shutDown();
    
    _isSetup = false;
    _i2cPort.stop();
}
 
bool VELM6030::isOpen(){
    return _isSetup;
    
};

uint8_t    VELM6030::getDevAddr(){
    return _i2cPort.getDevAddr();
};


// REG0x00, bit[0]
// This function powers down the Ambient Light Sensor. The light sensor will
// hold onto the last light reading which can be acessed while the sensor is
// shut down. 0.5 micro Amps are consumed while shutdown.
bool VELM6030::shutDown(){

  return _writeRegister(SETTING_REG, SD_MASK, SHUTDOWN , NO_SHIFT);

}

// REG0x00, bit[0]
// This function powers up the Ambient Light Sensor. The last value that was
// read during shut down will be overwritten on the sensor's subsequent read.
// After power up, a small 4ms delay is applied to give time for the internal
// osciallator and signal processor to power up.
bool VELM6030::powerOn(){

    bool success = _writeRegister(SETTING_REG, SD_MASK, POWER, NO_SHIFT);
    
    if(success){
        //delay 4 milliseconds.
       std::this_thread::sleep_for(4ms);
    }
    
    return success;
}




// REG0x03, bit[0]
// This function enables the current power save mode value and puts the Ambient
// Light Sensor into power save mode.
bool VELM6030::enablePowSave(){
    
    return _writeRegister(POWER_SAVE_REG, POW_SAVE_EN_MASK, ENABLE, NO_SHIFT);

}

// REG0x03, bit[0]
// This function disables the current power save mode value and pulls the Ambient
// Light Sensor out of power save mode.
bool VELM6030::disablePowSave(){

    return _writeRegister(POWER_SAVE_REG, POW_SAVE_EN_MASK, DISABLE, NO_SHIFT);

}

// REG0x03, bit[0]
// This function checks to see if power save mode is enabled or disabled.
bool VELM6030::readPowSavEnabled(bool &isEnabled){
    
    uint16_t regVal = 0;
    
    if(!_i2cPort.readWord(POWER_SAVE_REG, regVal))  return false;
    isEnabled =  regVal & (~POW_SAVE_EN_MASK)?true:false;
    
    return true;
}

// REG0x03, bit[2:1]
// This function sets the power save mode value. It takes a value of 1-4. Each
// incrementally higher value descreases the sampling rate of the sensor and so
// increases power saving. The datasheet suggests enabling these modes when
// continually sampling the sensor.
bool VELM6030::setPowSavMode(uint16_t modeVal){

  uint16_t bits;

  if (modeVal == 1)
    bits = 0;
  else if (modeVal == 2)
    bits = 1;
  else if (modeVal == 3)
    bits = 2;
  else if (modeVal == 4)
    bits = 3;
  else
      return false;

    return _writeRegister(POWER_SAVE_REG, POW_SAVE_MASK, bits, PSM_POS);

}



// REG0x00, bits [12:11]
// This function sets the gain for the Ambient Light Sensor. Possible values
// are 1/8, 1/4, 1, and 2. The highest setting should only be used if the
// sensors is behind dark glass, where as the lowest setting should be used in
// dark rooms. The datasheet suggests always leaving it at around 1/4 or 1/8.
bool VELM6030::setGain(float gainVal){

  uint16_t bits;

  if (gainVal == 1.00)
    bits = 0;
  else if (gainVal == 2.00)
    bits = 1;
  else if (gainVal == .125)
    bits = 2;
  else if (gainVal == .25)
    bits = 3;
  else
      return false;
  
 return  _writeRegister(SETTING_REG, GAIN_MASK, bits, GAIN_POS);

}

// REG0x00, bits [12:11]
// This function reads the gain for the Ambient Light Sensor. Possible values
// are 1/8, 1/4, 1, and 2. The highest setting should only be used if the
// sensors is behind dark glass, where as the lowest setting should be used in
// dark rooms. The datasheet suggests always leaving it at around 1/4 or 1/8.
bool VELM6030::readGain( float & gainOut ){
    
    uint16_t regVal = 0;
    
    if(!_i2cPort.readWord(SETTING_REG, regVal))  return false;
    regVal &= (~GAIN_MASK); // Invert the gain mask to _keep_ the gain
    regVal = (regVal >> GAIN_POS); // Move values to front of the line.
    
    switch (regVal) {
        case 0:
            gainOut = 1;
            break;
            
        case 1:
            gainOut = 2;
            break;
            
        case 2:
            gainOut = .125;
            break;
            
        case 3:
            gainOut = .25;
            break;
            
        default:
            return false;
            break;
    }
    
    return true;
}

// REG0x00, bits[9:6]
// This function sets the integration time (the saturation time of light on the
// sensor) of the ambient light sensor. Higher integration time leads to better
// resolution but slower sensor refresh times.
bool VELM6030::setIntegTime(uint16_t time){
 
  uint16_t bits;

  if (time == 100) // Default setting.
    bits = 0;
  else if (time == 200)
    bits = 1;
  else if (time == 400)
    bits = 2;
  else if (time == 800)
    bits = 3;
  else if (time == 50)
    bits = 8;
  else if (time == 25)
    bits = 12;
  else
      return false;

  return _writeRegister(SETTING_REG, INTEG_MASK, bits, INTEG_POS);
}

// REG0x00, bits[9:6]
// This function reads the integration time (the saturation time of light on the
// sensor) of the ambient light sensor. Higher integration time leads to better
// resolution but slower sensor refresh times.
bool VELM6030::readIntegTime(uint16_t &intTime){

    uint16_t regVal = 0;
    
    if(!_i2cPort.readWord(SETTING_REG, regVal))  return false;
    regVal &= (~INTEG_MASK);
    regVal = (regVal >> INTEG_POS);

    switch (regVal) {
        case 0:
            intTime = 100;
            break;
            
        case 1:
            intTime = 200;
            break;
            
        case 2:
            intTime = 400;
            break;
            
        case 3:
            intTime = 800;
            break;
            
        case 8:
            intTime = 50;
            break;
            
        case 12:
            intTime = 25;
            break;
   
        default:
            return false;
            break;
    }
    
    return true;
}


// REG[0x04], bits[15:0]
// This function gets the sensor's ambient light's lux value. The lux value is
// determined based on current gain and integration time settings. If the lux
// value exceeds 1000 then a compensation formula is applied to it.
bool VELM6030::readLight(uint32_t &luxOut){
    
    uint16_t lightBits;
    uint32_t luxVal;
    
    if( _i2cPort.readWord(AMBIENT_LIGHT_DATA_REG, lightBits)
       && _calculateLux(lightBits, luxVal)){
        
        if (luxVal > 1000)
            luxOut = _luxCompensation(luxVal);
        else
            luxOut = luxVal;
        return true;
        
    }
    return false;
}

// REG[0x05], bits[15:0]
// This function gets the sensor's ambient light's lux value. The lux value is
// determined based on current gain and integration time settings. If the lux
// value exceeds 1000 then a compensation formula is applied to it.
bool VELM6030::readWhiteLight(uint32_t &luxOut){
    
    
    uint16_t lightBits;
    uint32_t luxVal;
    
    if( _i2cPort.readWord(WHITE_LIGHT_DATA_REG, lightBits)
       && _calculateLux(lightBits, luxVal)){
        
        if (luxVal > 1000)
            luxOut = _luxCompensation(luxVal);
        else
            luxOut = luxVal;
        return true;
        
    }
    return false;
}


//MARK: - private functions


// This function compensates for lux values over 1000. From datasheet:
// "Illumination values higher than 1000 lx show non-linearity. This
// non-linearity is the same for all sensors, so a compensation forumla..."
// etc. etc.
uint32_t VELM6030::_luxCompensation(uint32_t _luxVal){

  // Polynomial is pulled from pg 10 of the datasheet.
  uint32_t _compLux = (.00000000000060135 * (pow(_luxVal, 4))) -
                      (.0000000093924 * (pow(_luxVal, 3))) +
                      (.000081488 * (pow(_luxVal,2))) +
                      (1.0023 * _luxVal);
  return _compLux;

}

// The lux value of the Ambient Light sensor depends on both the gain and the
// integration time settings. This function determines which conversion value
// to use by using the bit representation of the gain as an index to look up
// the conversion value in the correct integration time array. It then converts
// the value and returns it.
bool VELM6030::_calculateLux(uint16_t _lightBits, uint32_t& calculatedLux ){
    
    float _luxConv;
    uint8_t _convPos;
    float _gain;
    uint16_t _integTime;
    
    if(readGain(_gain)
       && readIntegTime(_integTime))
    {
        // Here the gain is checked to get the position of the conversion value
        // within the integration time arrays. These values also represent the bit
        // values for setting the gain.
        if (_gain == 2.00)
            _convPos = 0;
        else if (_gain == 1.00)
            _convPos = 1;
        else if (_gain == .25)
            _convPos = 2;
        else if (_gain == .125)
            _convPos = 3;
        else
            return false;
        
        // Here we check the integration time which determines which array we probe
        // at the position determined above.
        if(_integTime == 800)
            _luxConv = eightHIt[_convPos];
        else if(_integTime == 400)
            _luxConv = fourHIt[_convPos];
        else if(_integTime == 200)
            _luxConv = twoHIt[_convPos];
        else if(_integTime == 100)
            _luxConv = oneHIt[_convPos];
        else if(_integTime == 50)
            _luxConv = fiftyIt[_convPos];
        else if(_integTime == 25)
            _luxConv = twentyFiveIt[_convPos];
        else
            return false;
        
        // Multiply the value from the 16 bit register to the conversion value and return
        // it.
        calculatedLux = (_luxConv * _lightBits);
        return true;
    }
    return false;
}


// This function does the opposite calculation then the function above. The interrupt
// threshold values given by the user are dependent on the gain and
// intergration time settings. As a result the lux value needs to be
// calculated with the current settings and this function accomplishes
// that.
bool VELM6030::_calculateBits(uint32_t _luxVal, uint16_t & calculatedBits){
    float _luxConv;
    uint8_t _convPos;
    
    float _gain;
    uint16_t _integTime;
    
    if(readGain(_gain)
       && readIntegTime(_integTime))
    {
        
        // Here the gain is checked to get the position of the conversion value
        // within the integration time arrays. These values also represent the bit
        // values for setting the gain.
        if (_gain == 2.00)
            _convPos = 0;
        else if (_gain == 1.00)
            _convPos = 1;
        else if (_gain == .25)
            _convPos = 2;
        else if (_gain == .125)
            _convPos = 3;
        else
            return false;
        
        // Here we check the integration time which determines which array we probe
        // at the position determined above.
        if(_integTime == 800)
            _luxConv = eightHIt[_convPos];
        else if(_integTime == 400)
            _luxConv = fourHIt[_convPos];
        else if(_integTime == 200)
            _luxConv = twoHIt[_convPos];
        else if(_integTime == 100)
            _luxConv = oneHIt[_convPos];
        else if(_integTime == 50)
            _luxConv = fiftyIt[_convPos];
        else if(_integTime == 25)
            _luxConv = twentyFiveIt[_convPos];
        else
            return false;
        
        // Divide the value of lux bythe conversion value and return
        // it.
        calculatedBits = (_luxVal/_luxConv);
        return true;
    }
    return false;
}

// This function writes to a 16 bit register. Paramaters include the register's address, a mask
// for bits that are ignored, the bits to write, and the bits' starting
// position.
bool VELM6030::_writeRegister(uint8_t _wReg, uint16_t _mask,\
                                            uint16_t _bits, uint8_t _startPosition)
{
    uint16_t _i2cWrite;
    
    //  _i2cWrite = _readRegister(_wReg); // Get the current value of the register
    if(!_i2cPort.readWord(_wReg, _i2cWrite))  return false;
    _i2cWrite &= _mask; // Mask the position we want to write to.
    _i2cWrite |= (_bits << _startPosition);  // Place the given bits to the variable
  
//  _i2cPort->beginTransmission(_address); // Start communication.
//  _i2cPort->write(_wReg); // at register....
//  _i2cPort->write(_i2cWrite); // Write LSB to register...
//  _i2cPort->write(_i2cWrite >> 8); // Write MSB to register...
//  _i2cPort->endTransmission(); // End communcation.
    if(!_i2cPort.writeWord(_wReg, _i2cWrite))  return false;
  
    return true;;
}
