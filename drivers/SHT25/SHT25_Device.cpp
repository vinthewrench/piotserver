//
//  SHT25_Device.cpp
//  pIoTServer
//
//  Created by vinnie on 3/12/25.
//

#include "SHT25_Device.hpp"
#include "TimeStamp.hpp"
#include "LogMgr.hpp"
#include "PropValKeys.hpp"


constexpr string_view Driver_Version = "1.1.0 dev 0";

bool SHT25_Device::getVersion(string &str){
    str = string(Driver_Version);
    return true;
}

SHT25_Device::SHT25_Device(string devID) :SHT25_Device(devID, string()){};

SHT25_Device::SHT25_Device(string devID, string driverName){
    setDeviceID(devID, driverName);
  
    _state = INS_UNKNOWN;
    _lastQueryTime = {0,0};
   
    json j = {
        { PROP_DEVICE_MFG_URL, "https://sensirion.com/products/catalog/SHT25"},
        { PROP_DEVICE_MFG_PART, "SHT25 ±1.8% Digital humidity and temperature sensor"},
    };
    
    setProperties(j);
    _deviceState = DEVICE_STATE_UNKNOWN;
    _isSetup = false;
}

SHT25_Device::~SHT25_Device(){
    stop();
 }

bool SHT25_Device::initWithSchema(deviceSchemaMap_t deviceSchema){
    
    uint64_t delay = UINT64_MAX;
    
    for(const auto& [key, entry] : deviceSchema) {
        
        if(entry.units == DEGREES_C ){
            _resultKey_temperature = key;
            if(entry.queryDelay < delay)  delay = entry.queryDelay;
            _isSetup = true;
        }
        else if(entry.units == RH ){
            _resultKey_humidity = key;
            if(entry.queryDelay < delay)  delay = entry.queryDelay;
            _isSetup = true;
        }
        else if(entry.units == SERIAL_NO ){
            _resultKey_serialNo = key;
            _isSetup = true;
        }
        
    }
    _queryDelay = delay != UINT64_MAX? delay : default_queryDelay;
 
    _deviceState = DEVICE_STATE_DISCONNECTED;
    return _isSetup;
}


bool SHT25_Device::start(){
    bool status = false;
    int error = 0;
    
    if(!_deviceProperties[PROP_ADDRESS].is_string()){
        LOGT_DEBUG("SHT25_Device begin called with no %s property",string(PROP_ADDRESS).c_str());;
        return false;
    }
    
    if(_deviceID.size() == 0){
        LOGT_DEBUG("SHT25_Device has no deviceID");
        return  false;
    }
    
    string address  = _deviceProperties[PROP_ADDRESS];
    uint8_t i2cAddr = std::stoi(address.c_str(), 0, 16);
    
    if(!_isSetup){
        LOGT_DEBUG("SHT25_Device(%s) begin called before initWithKey ",address.c_str());
        return  false;
    }
    
    LOGT_DEBUG("SHT25_Device(%02X) begin %s",i2cAddr, _resultKey_temperature.c_str());
    status = _device.begin(i2cAddr, error);
    
    if(!status){
        LOGT_ERROR("SHT25_Device(%02X) begin FAILED: %s",i2cAddr,strerror(errno));
        _state = INS_INVALID;
        _deviceState = DEVICE_STATE_ERROR;
        return false;
    }
    
    uint8_t serialNo[8] = {0};

    status = _device.readSerialNumber(serialNo);
    if(!status){
        LOGT_ERROR("SHT25_Device(%02X) readSerialNumber FAILED: %s",i2cAddr,strerror(errno));
        _state = INS_INVALID;
        _deviceState = DEVICE_STATE_ERROR;
        return false;
    }

    _lastQueryTime = {0,0};
    _state = INS_IDLE;
    _deviceState = DEVICE_STATE_CONNECTED;
    
    _serialNo = hexString(serialNo, sizeof(serialNo));
    
    return true;
}
 
void SHT25_Device::stop(){
    
    LOGT_DEBUG("SHT25_Device(%02X) stop", _device.getDevAddr());

    _state = INS_UNKNOWN;
    _lastQueryTime = {0,0};

    if(_device.isOpen()){
        _device.stop();
    }
    _deviceState = DEVICE_STATE_DISCONNECTED;
}

bool SHT25_Device::setEnabled(bool enable){
   
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

bool SHT25_Device::isConnected(){
    return _device.isOpen();
}
 

bool SHT25_Device::getValues( keyValueMap_t &results){
    
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
           
            SHT25::SHT25_data data;
  
            if( _device.readSensor(data)){
                results[_resultKey_temperature] = to_string(data.temperature);
                results[_resultKey_humidity] = to_string(data.humidity);
                results[_resultKey_serialNo] = _serialNo;
                gettimeofday(&_lastQueryTime, NULL);
                hasData = true;
           }
        }
    }
    return hasData;
}
