//
//  QWIIC_RELAY_Device.cpp
//  pIoTServer
//
//  Created by vinnie on 3/16/25.
//

#include "QWIIC_RELAY_Device.hpp"
#include "TimeStamp.hpp"
#include "LogMgr.hpp"
#include "PropValKeys.hpp"



constexpr string_view Driver_Version = "1.1.0 dev 0";

bool QWIIC_RELAY_Device::getVersion(string &str){
    str = string(Driver_Version);
    return true;
}

QWIIC_RELAY_Device::QWIIC_RELAY_Device(string devID) :QWIIC_RELAY_Device(devID, string()){};

QWIIC_RELAY_Device::QWIIC_RELAY_Device(string devID, string driverName){
    setDeviceID(devID, driverName);
 
    json j = {
        { PROP_DEVICE_MFG_URL, " https://www.sparkfun.com/sparkfun-qwiic-dual-solid-state-relay.html"},
        { PROP_DEVICE_MFG_PART, "SparkFun Qwiic Relay"},
    };
    
    setProperties(j);
    _model =  QWIIC_RELAY::QWR_UNKNOWN;
    _deviceState = DEVICE_STATE_UNKNOWN;
    _isSetup = false;
}

QWIIC_RELAY_Device::~QWIIC_RELAY_Device(){
    stop();
 }

bool QWIIC_RELAY_Device::initWithSchema(deviceSchemaMap_t deviceSchema){
   
      for(const auto& [key, entry] : deviceSchema) {
           _pinMap[key] = entry.pinNo;
      }
      
      _isSetup = true;
      _deviceState = DEVICE_STATE_DISCONNECTED;

    return _isSetup;
}


bool QWIIC_RELAY_Device::start(){
    bool status = false;
    int error = 0;
        
    if(_deviceID.size() == 0){
        LOGT_DEBUG("QWIIC_RELAY_Device has no deviceID");
        return  false;
    }
    
    if( _deviceProperties[PROP_ADDRESS].is_string()){
        LOGT_DEBUG("QWIIC_RELAY_Device does not support alternet address");
        return false;
    }
    
    if(!_deviceProperties[PROP_DEVICE_TYPE].is_string()){
        LOGT_DEBUG("QWIIC_RELAY_Device begin called with no %s property",string(PROP_DEVICE_TYPE).c_str());;
        return false;
    }
    
    map<string_view, QWIIC_RELAY::qwr_model>  supported_devices = {
        {PROP_DEVICE_QWR_16566, QWIIC_RELAY::QWR_16566},
        {PROP_DEVICE_QWR_15093, QWIIC_RELAY::QWR_15093},
        {PROP_DEVICE_QWR_16810, QWIIC_RELAY::QWR_16810},
    };
    
    string devType = _deviceProperties[PROP_DEVICE_TYPE];
    
    if(!supported_devices.count(devType)){
        LOGT_DEBUG("QWIIC_RELAY_Device has no support for %s",devType.c_str());;
        return false;
    }
    
    _model = supported_devices[devType];
    
    string keyNames;
    for(const auto& [key, _] : _pinMap)  keyNames += ( key + " ");
  
    LOGT_DEBUG("QWIIC_RELAY_Device(%s) begin %s", devType.c_str(), keyNames.c_str());
    status = _device.begin(_model, false,  error);
    
    if(status){
        _device.allOff();
        _pinDidChange = true;
       _deviceState = DEVICE_STATE_CONNECTED;
    }
    else {
        LOGT_ERROR("QWIIC_RELAY_Device(%s) begin FAILED: %s",devType.c_str(),strerror(errno));
        _deviceState = DEVICE_STATE_ERROR;
    }
    return status;
}
 
  
void QWIIC_RELAY_Device::stop(){
    
    LOGT_DEBUG("QWIIC_RELAY_Device(%02X) stop", _device.getDevAddr());
 
    if(_device.isOpen()){
        _device.allOff();
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _pinDidChange = true;
       }
        _device.stop();
    }
    
     _deviceState = DEVICE_STATE_DISCONNECTED;
}

bool QWIIC_RELAY_Device::setEnabled(bool enable){
   
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

bool QWIIC_RELAY_Device::isConnected(){
    return _device.isOpen();
}
 

bool QWIIC_RELAY_Device::allOff(){
    
    bool status = false;
      
    if(_device.isOpen()){
        status = _device.allOff();
    }
    
    return status;
}



bool QWIIC_RELAY_Device::hasUpdates(){
   std::lock_guard<std::mutex> lock(_mutex);
   
   return _pinDidChange;
}

bool QWIIC_RELAY_Device::setValues(keyValueMap_t kv){
    
    if(!isConnected())
        return false;
  
    uint wins = 0;
    
    for(const auto& [key, valStr] : kv){
        
        if(_pinMap.count(key)){
            uint8_t pin = _pinMap[key];
            bool state = false;
            bool isBool = false;
            
            isBool =  stringToBool(valStr,state);
 
            if(!isBool) return false;
            if( _device.setRelay(pin, state)) wins++;
        }
    }
       
    if(wins>0)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _pinDidChange = true;
        return wins == kv.size();
    }

    return false;
}


bool QWIIC_RELAY_Device::getValues (keyValueMap_t &results){
    bool hasData = false;

    std::vector<bool> relayState;
 
    if(_device.relayState(relayState)){
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _pinDidChange = false;
        }
 
        for(uint8_t i = 0; i < relayState.size(); i++){
            for (auto& [key, value] : _pinMap) {
                if(i+1 == value){
                    results[key] = to_string(relayState[i]);
                    break;
                }
            }
          }
     }
 
    if(results.size() > 0 )
        hasData = true;

    return hasData;
}
