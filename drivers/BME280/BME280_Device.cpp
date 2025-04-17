//
//  BME280_Device.cpp
//  pIoTServer
//
//  Created by vinnie on 12/28/24.
//

#include "BME280_Device.hpp"
#include "TimeStamp.hpp"
#include "LogMgr.hpp"
#include "PropValKeys.hpp"



constexpr string_view Driver_Version = "1.1.0 dev 0";

bool BME280_Device::getVersion(string &str){
    str = string(Driver_Version);
    return true;
}

BME280_Device::BME280_Device(string devID) :BME280_Device(devID, string()){};

BME280_Device::BME280_Device(string devID, string driverName){
    setDeviceID(devID, driverName);
  
    _state = INS_UNKNOWN;
    _lastQueryTime = {0,0};
   
    json j = {
        { PROP_DEVICE_MFG_URL, "https://www.sparkfun.com/products/15440"},
        { PROP_DEVICE_MFG_PART, "SparkFun Atmospheric Sensor Breakout - BME280 (Qwiic)"},
    };
    
    setProperties(j);
    _deviceState = DEVICE_STATE_UNKNOWN;
    _isSetup = false;
}

BME280_Device::~BME280_Device(){
    stop();
 }

bool BME280_Device::initWithSchema(deviceSchemaMap_t deviceSchema){
    
    uint64_t delay = UINT64_MAX;
    
    for(const auto& [key, entry] : deviceSchema) {
        
        if(entry.units == DEGREES_C ){
            _resultKey_temperature = key;
            if(entry.queryDelay < delay)  delay = entry.queryDelay;
            _isSetup = true;
         } else if(entry.units == RH ){
            _resultKey_humidity = key;
             if(entry.queryDelay < delay)  delay = entry.queryDelay;
             _isSetup = true;
        } else if(entry.units == HPA ){
            _resultKey_pressure = key;
            if(entry.queryDelay < delay)  delay = entry.queryDelay;
            _isSetup = true;
        }
    }
    
    _queryDelay = delay != UINT64_MAX? delay : default_queryDelay;
 
    _deviceState = DEVICE_STATE_DISCONNECTED;
    return _isSetup;
}


bool BME280_Device::start(){
    bool status = false;
    int error = 0;
    
    if(!_deviceProperties[PROP_ADDRESS].is_string()){
        LOGT_DEBUG("BME280_Device begin called with no %s property",string(PROP_ADDRESS).c_str());;
        return false;
    }
    
    if(_deviceID.size() == 0){
        LOGT_DEBUG("BME280_Device has no deviceID");
        return  false;
    }

    string address  = _deviceProperties[PROP_ADDRESS];
    uint8_t i2cAddr = std::stoi(address.c_str(), 0, 16);
  
    if(!_isSetup){
        LOGT_DEBUG("BME280_Device(%s) begin called before initWithKey ",address.c_str());
        return  false;
    }
 
 
    LOGT_DEBUG("BME280_Device(%02X) begin %s",i2cAddr, _resultKey_temperature.c_str());
    status = _device.begin(i2cAddr, error);

    if(status){
        _lastQueryTime = {0,0};
        _state = INS_IDLE;
        _deviceState = DEVICE_STATE_CONNECTED;
    }
    else {
        LOGT_ERROR("BME280_Device(%02X) begin FAILED: %s",i2cAddr,strerror(errno));
       _state = INS_INVALID;
        _deviceState = DEVICE_STATE_ERROR;
    }
     return status;
}
 


  
void BME280_Device::stop(){
    
    LOGT_DEBUG("BME280_Device(%02X) stop", _device.getDevAddr());

    _state = INS_UNKNOWN;
    _lastQueryTime = {0,0};

    if(_device.isOpen()){
        _device.stop();
    }
    _deviceState = DEVICE_STATE_DISCONNECTED;
}

bool BME280_Device::setEnabled(bool enable){
   
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

bool BME280_Device::isConnected(){
    return _device.isOpen();
}
 

bool BME280_Device::getValues( keyValueMap_t &results){
    
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
            
            BME280::compensated_data data;
        
           if( _device.readSensor(data)){
                results[_resultKey_temperature] = to_string(data.temperature);
                results[_resultKey_humidity] = to_string(data.humidity);
                results[_resultKey_pressure] = to_string(data.pressure);
                
                gettimeofday(&_lastQueryTime, NULL);
                hasData = true;
           }
        }
    }
    return hasData;
}
