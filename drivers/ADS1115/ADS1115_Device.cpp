//
//  ADS1115_Device.cpp
//  pIoTServer
//
//  Created by vinnie on 4/8/25.
//

#include "ADS1115_Device.hpp"
#include "TimeStamp.hpp"
#include "LogMgr.hpp"
#include "PropValKeys.hpp"


constexpr string_view Driver_Version = "1.1.0 dev 0";

bool ADS1115_Device::getVersion(string &str){
    str = string(Driver_Version);
    return true;
}

ADS1115_Device::ADS1115_Device(string devID) :ADS1115_Device(devID, string()){};

ADS1115_Device::ADS1115_Device(string devID, string driverName){
    setDeviceID(devID, driverName);
    _state = INS_UNKNOWN;
    _deviceState = DEVICE_STATE_UNKNOWN;

    _lastQueryTime = {0,0};
    _isSetup = false;
    
    json j = {
         { PROP_DEVICE_MFG_URL, "https://www.ti.com/lit/ds/symlink/ads1115.pdf"},
         { PROP_DEVICE_MFG_PART, "ADS111x Ultra-Small, Low-Power, I2C-Compatible, 16-Bit ADC"},
     };

    setProperties(j);
  
}

ADS1115_Device::~ADS1115_Device(){
    stop();
 }

bool ADS1115_Device::initWithSchema(deviceSchemaMap_t deviceSchema){
  
    for(const auto& [key, entry] : deviceSchema) {
        if(entry.units == INT ){
            _resultKey = key;
            _queryDelay = entry.queryDelay != UINT64_MAX? entry.queryDelay : default_queryDelay;
            _isSetup = true;
             return true;
        }
    }
    
    _deviceState = DEVICE_STATE_DISCONNECTED;

    return false;
}

bool ADS1115_Device::start(){
    bool status = false;
    int error = 0;
    
    if(!_deviceProperties[PROP_ADDRESS].is_string()){
        LOGT_DEBUG("ADS1115_Device begin called with no %s property",string(PROP_ADDRESS).c_str());;
        return false;
    }
    
    if(_deviceID.size() == 0){
        LOGT_DEBUG("ADS1115_Device has no deviceID");
        return  false;
    }

    string address  = _deviceProperties[PROP_ADDRESS];
    uint8_t i2cAddr = std::stoi(address.c_str(), 0, 16);
  
    if(!_isSetup){
        LOGT_DEBUG("ADS1115_Device(%s) begin called before initWithKey ",address.c_str());
        return  false;
    }
 
    LOGT_DEBUG("ADS1115_Device(%02X) begin %s",i2cAddr, _resultKey.c_str());
    status = _device.begin(i2cAddr, error);

    if(status){
        _lastQueryTime = {0,0};
        _state = INS_IDLE;
        _deviceState = DEVICE_STATE_CONNECTED;
    }
    else {
        LOGT_ERROR("ADS1115_Device(%02X) begin FAILED: %s",i2cAddr,strerror(errno));
       _state = INS_INVALID;
        _deviceState = DEVICE_STATE_ERROR;
    }
     return status;
}
 


  
void ADS1115_Device::stop(){
    
    LOGT_DEBUG("ADS1115_Device(%02X) stop", _device.getDevAddr());

    _state = INS_UNKNOWN;
    _lastQueryTime = {0,0};

    if(_device.isOpen()){
        _device.stop();
    }
    _deviceState = DEVICE_STATE_DISCONNECTED;
}

bool ADS1115_Device::setEnabled(bool enable){
   
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


bool ADS1115_Device::isConnected(){
  
    return _device.isOpen();
}
 

bool ADS1115_Device::getValues( keyValueMap_t &results){
    
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
            
            /*
             // max out result
             if(rawData < _valEmpty) rawData = _valEmpty;
             else if (rawData > _valFull) rawData = _valFull;
             float depth = (float((rawData - _valEmpty)) /float(( _valFull - _valEmpty))) * 100.0;

             */
            uint16_t raw = 0;
            if( _device.analogRead(raw)){
                results[_resultKey] = to_string(raw);
                gettimeofday(&_lastQueryTime, NULL);
                hasData = true;
           }
        }
    }
    return hasData;
}
