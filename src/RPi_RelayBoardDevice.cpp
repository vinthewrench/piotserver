//
//  RPi_RelayBoardDevice.cpp
//  pIoTServer
//
//  Created by vinnie on 1/3/25.
//

#include "RPi_RelayBoardDevice.hpp"

#include "TimeStamp.hpp"
#include "LogMgr.hpp"
#include "PropValKeys.hpp"


constexpr string_view Driver_Version = "1.1.0 dev 0";

bool RPi_RelayBoardDevice::getVersion(string &str){
    str = string(Driver_Version);
    return true;
}


RPi_RelayBoardDevice::RPi_RelayBoardDevice(string devID) :RPi_RelayBoardDevice(devID, string()){};

RPi_RelayBoardDevice::RPi_RelayBoardDevice(string devID, string driverName){
    setDeviceID(devID, driverName);

     _isSetup = false;
    
    json j = {
         { PROP_DEVICE_MFG_URL, "https://www.waveshare.com/catalog/product/view/id/3616/s/rpi-relay-board-b/category/37/"},
         { PROP_DEVICE_MFG_PART, "Waveshare Raspberry Pi 8-ch Relay Expansion Board"},
     };

    setProperties(j);
    _deviceState = DEVICE_STATE_UNKNOWN;
 }

RPi_RelayBoardDevice::~RPi_RelayBoardDevice(){
    stop();
 }

bool RPi_RelayBoardDevice::initWithSchema(deviceSchemaMap_t deviceSchema){
  
    for(const auto& [key, entry] : deviceSchema) {
          _pinMap[key] = {
            .lineNo  = entry.pinNo,
            .direction = entry.readOnly
                        ?GPIO::GPIO_DIRECTION_INPUT
                        :GPIO::GPIO_DIRECTION_OUTPUT,
            .flags = (int) entry.flags
         };
      }

    _isSetup = true;
    return _isSetup;
}

bool RPi_RelayBoardDevice::start(){
    bool status = false;
    int error = 0;
    
    if(!_isSetup){
        LOGT_DEBUG("RPi_RelayBoardDevice begin called before initWithKey");
        return  false;
    }
    
    if(_deviceID.size() == 0){
        LOGT_DEBUG("RPi_RelayBoardDevice has no deviceID");
        return  false;
    }
 
    LOGT_DEBUG("RPi_RelayBoardDevice begin");
    
    vector<GPIO::gpio_pin_t> pins;
 
    // ONLY setup relay outputs for waveshare device
 //   auto filter = {5,6,13,16,19,20,21,26};
 
    for(auto [pinName, pin] :_pinMap) {
        
//        bool found (std::find(filter.begin(), filter.end(), pin.lineNo) != filter.end());
//        if(!found)continue;
//        
        pins.push_back(pin);
    }
        
    _pinDidChange = true;
    
    status = _gpio.begin( pins,error);
    
    if(status){
         _deviceState = DEVICE_STATE_CONNECTED;
     }
    else {
        LOGT_ERROR("RPi_RelayBoardDevice begin FAILED: %s",strerror(errno));
    }
     return status;
}
 


  
void RPi_RelayBoardDevice::stop(){
    
    LOGT_DEBUG("RPi_RelayBoardDevice stop");
  
    if(_gpio.isAvailable()){
        allOff();
        _gpio.stop();
    }
    _deviceState = DEVICE_STATE_DISCONNECTED;
 }

 
bool RPi_RelayBoardDevice::setEnabled(bool enable){
    
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


bool RPi_RelayBoardDevice::allOff(){

    GPIO::gpioStates_t gs ;
    
    for(auto p : _pinMap){
        if(p.second.direction == GPIO::GPIO_DIRECTION_OUTPUT)
            gs.push_back( make_pair(p.second.lineNo,  0));
  }
 
    return _gpio.set(gs);
}

bool RPi_RelayBoardDevice::isConnected(){
  
    return _gpio.isAvailable();
}
 
bool RPi_RelayBoardDevice::setValues(keyValueMap_t kv){

    GPIO::gpioStates_t gs ;
    
    for(const auto& [key, valStr] : kv){
        if(_pinMap.count(key)){
            
            GPIO::gpio_pin_t pin = _pinMap[key];
            if(pin.direction == GPIO::GPIO_DIRECTION_OUTPUT){
                bool state = false;
                bool isBool = false;
                
                isBool =  stringToBool(valStr,state);
                if(!isBool) return false;
                gs.push_back(make_pair(pin.lineNo,state));
            }
            
        }
    }
    if(gs.size()){
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _pinDidChange = true;
        }
        return _gpio.set(gs);
    }
    
    return false;
}


bool RPi_RelayBoardDevice::getValues (keyValueMap_t &results){
    
    std::lock_guard<std::mutex> lock(_mutex);
     bool hasData = false;
    _pinDidChange = false;

    if(!_gpio.isAvailable())
        return false;
 
    GPIO::gpioStates_t gs;
    if( _gpio.get(gs)){
        for(const auto& [lineNo, state] : gs) {
            for(auto [pinName, p] :_pinMap) {
                if(lineNo == p.lineNo){
                    results[pinName] = to_string(state);
//#warning DEBUG
//       if(pinName == "RAIN_SENSOR") results[pinName] = to_string(true);
                }
             }
        }
        hasData = true;
    }
  
    return hasData;
}

 //bool RPi_RelayBoardDevice::hasUpdates(){
//    std::lock_guard<std::mutex> lock(_mutex);
//    
//    return _pinDidChange;
//}
