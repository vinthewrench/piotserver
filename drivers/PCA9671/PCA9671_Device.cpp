//
//  PCA9671_Device.cpp
//  pIoTServer
//
//  Created by vinnie on 1/3/25.
//

#include "PCA9671_Device.hpp"

#include "TimeStamp.hpp"
#include "LogMgr.hpp"
#include "PropValKeys.hpp"

/*
    
https://www.iascaled.com/store/Qwiic/QwiicInputOutput/I2C-RELAY16-QWIIC
 
 https://www.sainsmart.com/products/16-channel-12v-relay-module
 
https://www.nxp.com/products/interfaces/ic-spi-i3c-interface-devices/general-purpose-i-o-gpio/remote-16-bit-i-o-expander-for-fm-plus-ic-bus-with-reset:PCA9671

*/



constexpr string_view Driver_Version = "1.1.0 dev 0";

bool PCA9671_Device::getVersion(string &str){
    str = string(Driver_Version);
   return true;
}

PCA9671_Device::PCA9671_Device(string devID) :PCA9671_Device(devID, string()){};
 
PCA9671_Device::PCA9671_Device(string devID, string driverName){
    setDeviceID(devID, driverName);
     _isSetup = false;

    json j = {
        { PROP_DEVICE_MFG_URL, {"https://www.iascaled.com/store/Qwiic/QwiicInputOutput/I2C-RELAY16-QWIIC",
            "https://www.sainsmart.com/products/16-channel-12v-relay-module",
            "https://www.nxp.com/products/interfaces/ic-spi-i3c-interface-devices/general-purpose-i-o-gpio/remote-16-bit-i-o-expander-for-fm-plus-ic-bus-with-reset:PCA9671"}},
         { PROP_DEVICE_MFG_PART, "Remote 16-Bit I/O Expander for I²C-Bus"},
     };
    setProperties(j);
  
    _deviceState = DEVICE_STATE_UNKNOWN;

}

PCA9671_Device::~PCA9671_Device(){
    stop();
 }

bool PCA9671_Device::initWithSchema(deviceSchemaMap_t deviceSchema){
  
    for(const auto& [key, entry] : deviceSchema) {
         _pinMap[key] = entry.pinNo;
    }
    
    _isSetup = true;
    _deviceState = DEVICE_STATE_DISCONNECTED;

    return _isSetup;
}

bool PCA9671_Device::start(){
    bool status = false;
    int error = 0;
    
    if(!_deviceProperties[PROP_ADDRESS].is_string()){
        LOGT_DEBUG("PCA9671_Device begin called with no %s property",string(PROP_ADDRESS).c_str());;
        return false;
    }
    
    if(_deviceID.size() == 0){
        LOGT_DEBUG("PCA9671_Device has no deviceID");
        return  false;
    }

    string address  = _deviceProperties[PROP_ADDRESS];
    uint8_t i2cAddr = std::stoi(address.c_str(), 0, 16);
    
    if(!_isSetup){
        LOGT_DEBUG("PCA9671_Device(%s) begin called before initWithKey ",address.c_str());
        return  false;
    }
    
    LOGT_DEBUG("PCA9671_Device(%02X) begin",i2cAddr);
    status = _device.begin(i2cAddr, error);
    
    if(status){
        _device.allOff();
        _pinDidChange = true;
        _deviceState = DEVICE_STATE_CONNECTED;
    }
    else {
        LOGT_ERROR("PCA9671_Device begin FAILED: %s",strerror(errno));
    }
    return status;
}
 


  
void PCA9671_Device::stop(){
    
    LOGT_DEBUG("PCA9671_Device  stop");
  
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

bool PCA9671_Device::setEnabled(bool enable){
   
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


bool PCA9671_Device::allOff(){
   
    bool status = false;
 
    if(_device.isOpen()){
        status = _device.allOff();
    }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _pinDidChange = true;
    }
 
    return status;
}

bool PCA9671_Device::isConnected(){
  
    return _device.isOpen();
}
 
bool PCA9671_Device::setValues(keyValueMap_t kv){
    
    if(!isConnected())
        return false;
    
    PCA9671::pinStates_t ps ;
    
    for(const auto& [key, valStr] : kv){
        
        if(_pinMap.count(key)){
            uint8_t pin = _pinMap[key];
            bool state = false;
            bool isBool = false;
            
            isBool =  stringToBool(valStr,state);
 
            if(!isBool) return false;
            ps.push_back( make_pair(pin,state));
        }
    }
    if(ps.size()){
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _pinDidChange = true;
        }
  
        return _device.setRelayStates(ps);
    }
    
    return false;
}


bool PCA9671_Device::getValues (keyValueMap_t &results){
    
    std::lock_guard<std::mutex> lock(_mutex);
     bool hasData = false;
    _pinDidChange = false;

    if(!isConnected()){
        // return zeroed states
        for(auto p : _pinMap){
                results[p.first] = to_string(0);
            }
        return true;
     }
 
    PCA9671::pinStates_t ps ;
    
    if( _device.getRelayStates(ps)){
        for(const auto& [relay, state] : ps) {
            for(auto p : _pinMap){
                if(relay == p.second){
                    results[p.first] = to_string(state);
                }
            }
        }
        hasData = true;
    }
        
    return hasData;
}

 
bool PCA9671_Device::hasUpdates(){
    std::lock_guard<std::mutex> lock(_mutex);
    
    return _pinDidChange;
}
