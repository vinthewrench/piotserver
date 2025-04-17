//
//  QwiicButton_Device.cpp
//  pIoTServer
//
//  Created by vinnie on 1/20/25.
//

#include "QwiicButton_Device.hpp"

#include "TimeStamp.hpp"
#include "LogMgr.hpp"
#include "PropValKeys.hpp"


constexpr string_view Driver_Version = "1.1.0 dev 0";

bool QwiicButton_Device::getVersion(string &str){
    str = string(Driver_Version);
    return true;
}

QwiicButton_Device::QwiicButton_Device(string devID) :QwiicButton_Device(devID, string()){};

QwiicButton_Device::QwiicButton_Device(string devID, string driverName){
    setDeviceID(devID, driverName);
   _state = INS_UNKNOWN;
    _lastQueryTime = {0,0};
    _isSetup = false;
    
      json j = {
        { PROP_DEVICE_MFG_URL, {"https://www.sparkfun.com/sparkfun-qwiic-button-green-led.html"
        }},
         { PROP_DEVICE_MFG_PART, "SparkFun Qwiic Button"},
     };
    setProperties(j);
    _deviceState = DEVICE_STATE_UNKNOWN;
}

QwiicButton_Device::~QwiicButton_Device(){
    stop();
 }

bool QwiicButton_Device::initWithSchema(deviceSchemaMap_t deviceSchema){
    
    uint64_t delay = UINT64_MAX;
    
    for(const auto& [key, entry] : deviceSchema) {
        
        if(entry.units == BOOL ){
            _buttonState = key;
            if(entry.queryDelay < delay)  delay = entry.queryDelay;
            _isSetup = true;
        }
        else if(entry.units == STRING ){
            _ledState = key;
            if(entry.queryDelay < delay)  delay = entry.queryDelay;
            _isSetup = true;
        }
    }
    
    _queryDelay = delay != UINT64_MAX? delay : default_queryDelay;
    
    _deviceState = DEVICE_STATE_DISCONNECTED;
    return _isSetup;
}
 

bool QwiicButton_Device::start(){
    bool status = false;
    int error = 0;
    
    if(!_deviceProperties[PROP_ADDRESS].is_string()){
        LOGT_DEBUG("QwiicButton_Device begin called with no %s property",string(PROP_ADDRESS).c_str());;
        return false;
    }
    string address  = _deviceProperties[PROP_ADDRESS];
    uint8_t i2cAddr = std::stoi(address.c_str(), 0, 16);
  
    if(!_isSetup){
        LOGT_DEBUG("QwiicButton_Device(%s) begin called before initWithKey ",address.c_str());
        return  false;
    }
 
    if(_deviceID.size() == 0){
        LOGT_DEBUG("QwiicButton_Device has no deviceID");
        return  false;
    }

    LOGT_DEBUG("QwiicButton_Device(%02X) begin %s",i2cAddr, _buttonState.c_str());
    status = _device.begin(i2cAddr, error);
     
    if(status){
        _lastQueryTime = {0,0};
        _state = INS_IDLE;
        _deviceState = DEVICE_STATE_CONNECTED;
        _device.LEDoff();
    }
    else {
        LOGT_ERROR("QwiicButton_Device(%02X) begin FAILED: %s",i2cAddr,strerror(errno));
       _state = INS_INVALID;
        _deviceState = DEVICE_STATE_ERROR;
    }
     return status;
}
 


  
void QwiicButton_Device::stop(){
    
    LOGT_DEBUG("QwiicButton_Device(%02X) stop", _device.getDevAddr());

    _state = INS_UNKNOWN;
    _lastQueryTime = {0,0};

    if(_device.isOpen()){
        _device.stop();
    }
    _deviceState = DEVICE_STATE_DISCONNECTED;
}

bool QwiicButton_Device::allOff(){
    if(_device.isOpen()){
        _device.LEDoff();
    }

    return true;
}


bool QwiicButton_Device::setEnabled(bool enable){
   
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

bool QwiicButton_Device::isConnected(){
    return _device.isOpen();
}
 

bool QwiicButton_Device::getValues( keyValueMap_t &results){
    
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
         
//            uint16_t rawData = 0;
//    
//            auto gain = MCP3427::GAIN_1X;
//            auto adcBits = MCP3427::ADC_16_BITS;
//            
//            if(_device.analogRead(    rawData, 0, gain, adcBits)) {
//    
//                results[_resultKey] = to_string(rawData);
//                gettimeofday(&_lastQueryTime, NULL);
//                hasData = true;
//
//           }
        }
    }
    return hasData;
}



bool QwiicButton_Device::setValues(keyValueMap_t kv){
 
    bool success = false;
    
    for(const auto& [key, valStr] : kv){
        
        if(key == _ledState){
            
            bool state = false;
            bool isBool = false;
            
            isBool =  stringToBool(valStr,state);
            if(isBool){
                if(state)
                    success = _device.LEDon();
                else
                    success =_device.LEDoff();
                continue;
            }
            else  if (isNumberString(valStr)){
                
                char   *p;
                double number = strtold(valStr.c_str(), &p);
                if(*p == 0){
                    
                    if(number > 100) number = 100;
                    uint8_t bright =  255 * (number/100);
                    success =_device.LEDon(bright);
                    continue;
                }
            }
            else {
                uint8_t brightness = 0;
                uint16_t cycleTime = 0;
                uint16_t offTime = 0;
                int n;
 
                if( sscanf(valStr.c_str(), "%hhd,%hd,%hd %n",
                           &brightness, &cycleTime, &offTime ,&n) == 3){
                 if(brightness > 100) brightness = 100;
                        
                    uint8_t bright =  255 * ( (double) brightness/100);
                    success =_device.LEDconfig(bright, cycleTime, offTime );
                    continue;
                    
                }
    
            }
        }
    }
    
    return success;
}
