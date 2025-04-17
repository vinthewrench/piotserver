//
//  TankDepth_Device.cpp
//  pIoTServer
//
//  Created by vinnie on 4/8/25.
//

#include "TankDepth_Device.hpp"
#include "TimeStamp.hpp"
#include "LogMgr.hpp"
#include "PropValKeys.hpp"
 
constexpr string_view Driver_Version = "1.1.0 dev 0";

bool TankDepth_Device::getVersion(string &str){
    str = Driver_Version;
    return true;
}

TankDepth_Device::TankDepth_Device(string devID) :TankDepth_Device(devID, string()){};

TankDepth_Device::TankDepth_Device(string devID, string driverName){
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

TankDepth_Device::~TankDepth_Device(){
    stop();
 }

bool TankDepth_Device::initWithSchema(deviceSchemaMap_t deviceSchema){
    
    
    for(const auto& [key, entry] : deviceSchema) {
        
        if(entry.units == PERCENT ){
            _resultKey = key;
            _queryDelay = entry.queryDelay != UINT64_MAX? entry.queryDelay : default_queryDelay;
            
            if(!entry.otherProps.is_null() ){
                json j = entry.otherProps;
                
                if(j.count(TANK_EMPTY)  && j.count(TANK_FULL) && j.count(TANK_GALS)){
                    _tankEmpty = j[TANK_EMPTY];
                    _tankFull = j[TANK_FULL];
                    _tankGals = j[TANK_GALS];
                    _isSetup = true;
                }
                return true;
            }
        }
    }
    
     LOGT_DEBUG("TankDepth_Device missing properties");
  
    _deviceState = DEVICE_STATE_DISCONNECTED;
    
    return false;
}

bool TankDepth_Device::start(){
    bool status = false;
    int error = 0;
    
    if(!_deviceProperties[PROP_ADDRESS].is_string()){
        LOGT_DEBUG("TankDepth_Device begin called with no %s property",string(PROP_ADDRESS).c_str());;
        return false;
    }
    
    if(_deviceID.size() == 0){
        LOGT_DEBUG("TankDepth_Device has no deviceID");
        return  false;
    }

    string address  = _deviceProperties[PROP_ADDRESS];
    uint8_t i2cAddr = std::stoi(address.c_str(), 0, 16);
  
    if(!_isSetup){
        LOGT_DEBUG("TankDepth_Device(%s) begin called before initWithKey ",address.c_str());
        return  false;
    }
 
    LOGT_DEBUG("TankDepth_Device(%02X) begin %s",i2cAddr, _resultKey.c_str());
    status = _device.begin(i2cAddr, error);

    if(status){
        _lastQueryTime = {0,0};
        _state = INS_IDLE;
        _deviceState = DEVICE_STATE_CONNECTED;
    }
    else {
        LOGT_ERROR("TankDepth_Device(%02X) begin FAILED: %s",i2cAddr,strerror(errno));
       _state = INS_INVALID;
        _deviceState = DEVICE_STATE_ERROR;
    }
     return status;
}
 


  
void TankDepth_Device::stop(){
    
    LOGT_DEBUG("TankDepth_Device(%02X) stop", _device.getDevAddr());

    _state = INS_UNKNOWN;
    _lastQueryTime = {0,0};

    if(_device.isOpen()){
        _device.stop();
    }
    _deviceState = DEVICE_STATE_DISCONNECTED;
}

bool TankDepth_Device::setEnabled(bool enable){
   
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


bool TankDepth_Device::isConnected(){
  
    return _device.isOpen();
}
 

bool TankDepth_Device::getValues( keyValueMap_t &results){
    
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
            
            uint16_t raw = 0;
            if( _device.analogRead(raw)){
            
                if(raw < _tankEmpty) raw = _tankEmpty;
                else if (raw > _tankFull)  raw = _tankFull;
                float depth = (float((raw - _tankEmpty)) /float(( _tankFull - _tankEmpty))) * 100.0;
                results[_resultKey] = to_string(depth);
            
                // debug tool to help set _tankFull/_tankEmpty
                results[_resultKey + "_RAW"] = to_string(raw);
                results[_resultKey + "_GALS"] = to_string(_tankGals * (depth * .01) );
  
                gettimeofday(&_lastQueryTime, NULL);
                hasData = true;
           }
        }
    }
    return hasData;
}
