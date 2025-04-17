//
//  MCP3427_Device.cpp
//  pIoTServer
//
//  Created by vinnie on 12/29/24.
//

#include "MCP3427_Device.hpp"
#include "TimeStamp.hpp"
#include "LogMgr.hpp"
#include "PropValKeys.hpp"

constexpr string_view Driver_Version = "1.1.0 dev 0";

bool MCP3427_Device::getVersion(string &str){
    str = string(Driver_Version);
    return true;
}

MCP3427_Device::MCP3427_Device(string devID) :MCP3427_Device(devID, string()){};

MCP3427_Device::MCP3427_Device(string devID, string driverName){
    setDeviceID(devID, driverName);
  
    _state = INS_UNKNOWN;
    _lastQueryTime = {0,0};
    _isSetup = false;
    
      json j = {
        { PROP_DEVICE_MFG_URL, {"https://www.iascaled.com/store/QwiicADC/I2C-MCP3427",
            "https://ww1.microchip.com/downloads/en/DeviceDoc/22226a.pdf",
            "https://www.vinthewrench.com/p/using-a-raspberry-pi-to-remotely"
        }},
         { PROP_DEVICE_MFG_PART, "2-Channel 16-Bit I2C/Qwiic Analog to Digital Converter"},
     };
    setProperties(j);
    _deviceState = DEVICE_STATE_UNKNOWN;
}

MCP3427_Device::~MCP3427_Device(){
    stop();
 }

bool MCP3427_Device::initWithSchema(deviceSchemaMap_t deviceSchema){
    _deviceState = DEVICE_STATE_DISCONNECTED;
    return false;
}

bool MCP3427_Device::start(){
    bool status = false;
    int error = 0;
    
    if(!_deviceProperties[PROP_ADDRESS].is_string()){
        LOGT_DEBUG("MCP3427_Device begin called with no %s property",string(PROP_ADDRESS).c_str());;
        return false;
    }
    string address  = _deviceProperties[PROP_ADDRESS];
    uint8_t i2cAddr = std::stoi(address.c_str(), 0, 16);
  
    if(!_isSetup){
        LOGT_DEBUG("MCP3427_Device(%s) begin called before initWithKey ",address.c_str());
        return  false;
    }
 
    if(_deviceID.size() == 0){
        LOGT_DEBUG("MCP3427_Device has no deviceID");
        return  false;
    }

    LOGT_DEBUG("MCP3427_Device(%02X) begin %s",i2cAddr, _resultKey.c_str());
    status = _device.begin(i2cAddr, error);
     
    if(status){
        _lastQueryTime = {0,0};
        _state = INS_IDLE;
        _deviceState = DEVICE_STATE_CONNECTED;
    }
    else {
        LOGT_ERROR("MCP3427_Device(%02X) begin FAILED: %s",i2cAddr,strerror(errno));
       _state = INS_INVALID;
        _deviceState = DEVICE_STATE_ERROR;
    }
     return status;
}
 


  
void MCP3427_Device::stop(){
    
    LOGT_DEBUG("MCP3427_Device(%02X) stop", _device.getDevAddr());

    _state = INS_UNKNOWN;
    _lastQueryTime = {0,0};

    if(_device.isOpen()){
        _device.stop();
    }
    _deviceState = DEVICE_STATE_DISCONNECTED;
}


bool MCP3427_Device::setEnabled(bool enable){
   
   if(enable){
       _isEnabled = true;
       
       if( _deviceState == DEVICE_STATE_CONNECTED){
           return true;
       }
       
       // force restart
       stop();
       
       bool success = start();
       return success;
   }
   
   _isEnabled = false;
   if(_deviceState == DEVICE_STATE_CONNECTED){
       stop();
   }
   return true;
}

bool MCP3427_Device::isConnected(){
    return _device.isOpen();
}
 

bool MCP3427_Device::getValues( keyValueMap_t &results){
    
    bool hasData = false;
    
    if(!isConnected()) {
        return false;
    }
    
    if(_state == INS_IDLE){
        
        bool shouldQuery = false;
        
        if(_lastQueryTime.tv_sec == 0 &&  _lastQueryTime.tv_usec == 0 ){
            shouldQuery = true;
        } else {
            
            timeval now, diff;
            gettimeofday(&now, NULL);
            timersub(&now, &_lastQueryTime, &diff);
            
            if(diff.tv_sec >=  _queryDelay  ) {
                shouldQuery = true;
            }
        }
        
        if(shouldQuery){
         
            uint16_t rawData = 0;
    
            auto gain = MCP3427::GAIN_1X;
            auto adcBits = MCP3427::ADC_16_BITS;
            
            if(_device.analogRead(    rawData, 0, gain, adcBits)) {
    
                results[_resultKey] = to_string(rawData);
                gettimeofday(&_lastQueryTime, NULL);
                hasData = true;
           }
        }
    }
    return hasData;
}

