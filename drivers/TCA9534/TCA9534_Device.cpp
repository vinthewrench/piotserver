//
//  TCA9534_Device.cpp
//  pIoTServer
//
//  Created by vinnie on 1/9/25.
//

#include "TCA9534_Device.hpp"
#include "TimeStamp.hpp"
#include "LogMgr.hpp"
#include "PropValKeys.hpp"

/*
    
https://www.iascaled.com/store/Qwiic/QwiicInputOutput/I2C-RELAY16-QWIIC
 
 https://www.sainsmart.com/products/16-channel-12v-relay-module
 
https://www.nxp.com/products/interfaces/ic-spi-i3c-interface-devices/general-purpose-i-o-gpio/remote-16-bit-i-o-expander-for-fm-plus-ic-bus-with-reset:PCA9671

*/


constexpr string_view Driver_Version = "1.1.0 dev 0";

bool TCA9534_Device::getVersion(string &str){
    str = string(Driver_Version);
    return true;
}

TCA9534_Device::TCA9534_Device(string devID) :TCA9534_Device(devID, string()){};

TCA9534_Device::TCA9534_Device(string devID, string driverName){
    setDeviceID(devID, driverName);
 
    json j = {
        { PROP_DEVICE_MFG_URL, {"https://www.sparkfun.com/sparkfun-qwiic-gpio.html",
            "https://www.ti.com/lit/ds/symlink/tca9534.pdf?ts=1736426837787"
        }},
         { PROP_DEVICE_MFG_PART, "SparkFun Qwiic GPIO"},
     };
    setProperties(j);
  
    _deviceState = DEVICE_STATE_UNKNOWN;

    _isSetup = false;
}

TCA9534_Device::~TCA9534_Device(){
    stop();
 }

bool TCA9534_Device::setEnabled(bool enable){
   
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

bool TCA9534_Device::initWithSchema(deviceSchemaMap_t deviceSchema){
  
    for(const auto& [key, entry] : deviceSchema) {
         _pinMap[key] = entry.pinNo;
    }
    
    _isSetup = true;
    _deviceState = DEVICE_STATE_DISCONNECTED;

    return _isSetup;
}



bool TCA9534_Device::start(){
    bool status = false;
    int error = 0;
   
    if(!_deviceProperties[PROP_ADDRESS].is_string()){
        LOGT_DEBUG("TCA9534_Device begin called with no %s property",string(PROP_ADDRESS).c_str());;
        return false;
    }
    string address  = _deviceProperties[PROP_ADDRESS];
    uint8_t i2cAddr = std::stoi(address.c_str(), 0, 16);
  
    if(!_isSetup){
        LOGT_DEBUG("TCA9534_Device(%s) begin called before initWithKey ",address.c_str());
        return  false;
    }
 
    if(_deviceID.size() == 0){
        LOGT_DEBUG("TCA9534_Device has no deviceID");
        return  false;
    }

    LOGT_DEBUG("TCA9534_Device(%02X) begin",i2cAddr);
    status = _device.begin(i2cAddr, error);
    
    if(status){
//        _device.allOff();
        _deviceState = DEVICE_STATE_CONNECTED;
     }
    else {
        LOGT_ERROR("TCA9534_Device begin FAILED: %s",strerror(errno));
    }
     return status;
}
 


  
void TCA9534_Device::stop(){
    
    LOGT_DEBUG("TCA9534_Device  stop");
  
    if(_device.isOpen()){
//        _device.allOff();
        _device.stop();
    }
    
    _deviceState = DEVICE_STATE_DISCONNECTED;

 }

bool TCA9534_Device::allOff(){
   
    bool status = false;
 
    if(_device.isOpen()){
 //       status = _device.allOff();
    }
    
    return status;
}

bool TCA9534_Device::isConnected(){
  
    return true;
    return _device.isOpen();
}
 
bool TCA9534_Device::setValues(keyValueMap_t kv){

    if(!isConnected())
        return false;
    
 
    return false;
}


bool TCA9534_Device::getValues (keyValueMap_t &results){
    
    bool hasData = false;
    
    if(!isConnected())
        return false;
        
    return hasData;
}
