//
//  TMP10X_Device.cpp
//  pIoTServer
//
//  Created by vinnie on 12/28/24.
//

#include "TMP10X_Device.hpp"
#include "TimeStamp.hpp"
#include "LogMgr.hpp"
#include "PropValKeys.hpp"


constexpr string_view Driver_Version = "1.1.0 dev 0";

bool TMP10X_Device::getVersion(string &str){
    str = string(Driver_Version);
    return true;
}

TMP10X_Device::TMP10X_Device(string devID) :TMP10X_Device(devID, string()){};

TMP10X_Device::TMP10X_Device(string devID, string driverName){
    setDeviceID(devID, driverName);
  
    _state = INS_UNKNOWN;
    _deviceState = DEVICE_STATE_UNKNOWN;

    _lastQueryTime = {0,0};
    _isSetup = false;
    
    json j = {
         { PROP_DEVICE_MFG_URL, "https://www.sparkfun.com/products/16304"},
         { PROP_DEVICE_MFG_PART, "SparkFun Digital Temperature Sensor - TMP10X (Qwiic)"},
     };

    setProperties(j);
  
}

TMP10X_Device::~TMP10X_Device(){
    stop();
 }

bool TMP10X_Device::initWithSchema(deviceSchemaMap_t deviceSchema){
  
    for(const auto& [key, entry] : deviceSchema) {
        if(entry.units == DEGREES_C ){
            _resultKey_temperature = key;
            _queryDelay = entry.queryDelay != UINT64_MAX? entry.queryDelay : default_queryDelay;
            _isSetup = true;
             return true;
        }
    }
    
    _deviceState = DEVICE_STATE_DISCONNECTED;

    return false;
}

bool TMP10X_Device::start(){
    bool status = false;
    int error = 0;
    
    if(!_deviceProperties[PROP_ADDRESS].is_string()){
        LOGT_DEBUG("TMP10X_Device begin called with no %s property",string(PROP_ADDRESS).c_str());;
        return false;
    }
    
    if(_deviceID.size() == 0){
        LOGT_DEBUG("TMP10X_Device has no deviceID");
        return  false;
    }

    string address  = _deviceProperties[PROP_ADDRESS];
    uint8_t i2cAddr = std::stoi(address.c_str(), 0, 16);
  
    if(!_isSetup){
        LOGT_DEBUG("TMP10X_Device(%s) begin called before initWithKey ",address.c_str());
        return  false;
    }
 
    LOGT_DEBUG("TMP10X_Device(%02X) begin %s",i2cAddr, _resultKey_temperature.c_str());
    status = _device.begin(i2cAddr, error);

    if(status){
        _lastQueryTime = {0,0};
        _state = INS_IDLE;
        _deviceState = DEVICE_STATE_CONNECTED;
    }
    else {
        LOGT_ERROR("TMP10X_Device(%02X) begin FAILED: %s",i2cAddr,strerror(errno));
       _state = INS_INVALID;
        _deviceState = DEVICE_STATE_ERROR;
    }
     return status;
}
 


  
void TMP10X_Device::stop(){
    
    LOGT_DEBUG("TMP10X_Device(%02X) stop", _device.getDevAddr());

    _state = INS_UNKNOWN;
    _lastQueryTime = {0,0};

    if(_device.isOpen()){
        _device.stop();
    }
    _deviceState = DEVICE_STATE_DISCONNECTED;
}

bool TMP10X_Device::setEnabled(bool enable){
   
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


bool TMP10X_Device::isConnected(){
  
    return _device.isOpen();
}
 

bool TMP10X_Device::getValues( keyValueMap_t &results){
    
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
            
            float tempC = 0;
            if( _device.readTempC(tempC)){
                results[_resultKey_temperature] = to_string(tempC);
                gettimeofday(&_lastQueryTime, NULL);
                hasData = true;
           }
        }
    }
    return hasData;
}
