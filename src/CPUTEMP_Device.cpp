//
//  CPUTEMP_Device.cpp
//  piServer
//
//  Created by vinnie on 12/28/24.
//

#include "CPUTEMP_Device.hpp"
#include "TimeStamp.hpp"
#include "LogMgr.hpp"
#include "PropValKeys.hpp"

CPUTEMP_Device::CPUTEMP_Device(){
    _state = INS_UNKNOWN;
    _lastQueryTime = {0,0};
    _isSetup = false;
    
    json j = {
        { PROP_DEVICE_MFG_URL, {"/sys/class/thermal/thermal_zone0/temp",
            "https://pip.raspberrypi.com/categories/685-whitepapers-app-notes/documents/RP-003608-WP/Cooling-a-Raspberry-Pi-device.pdf",
            "https://www.raspberrypi.com/products/active-cooler/"
        }},
        { PROP_DEVICE_MFG_PART, "Raspberry Pi Active Cooler"},
    };
    setProperties(j);
    _deviceState = DEVICE_STATE_UNKNOWN;
 }

CPUTEMP_Device::~CPUTEMP_Device(){
    stop();
 }

bool CPUTEMP_Device::initWithSchema(deviceSchemaMap_t deviceSchema){
    
    uint64_t delay = UINT64_MAX;
    
    for(const auto& [key, entry] : deviceSchema) {
        if(entry.units == DEGREES_C ){
            _resultKey_temp = key;
            if(entry.queryDelay < delay)  delay = entry.queryDelay;
            _isSetup = true;
        }
        else if(entry.units == INT ){
            _resultKey_cooler = key;
            if(entry.queryDelay < delay)  delay = entry.queryDelay;
            _isSetup = true;
        }
        
    }
    _queryDelay = delay != UINT64_MAX? delay : default_queryDelay;
    _deviceState = DEVICE_STATE_DISCONNECTED;
    
    _isSetup = true;
    return _isSetup;
}
 

bool CPUTEMP_Device::hasKey(string key){
    return
    (key == _resultKey_temp)
    || (key == _resultKey_cooler);
}

bool CPUTEMP_Device::start(){
  
    if(!_isSetup){
        LOGT_DEBUG("CPUTEMP_Device begin called before initWithKey");
        return  false;
    }

    LOGT_DEBUG("CPUTEMP_Device(%s) begin", _resultKey_temp.c_str());
 
    _lastQueryTime = {0,0};
    _state = INS_IDLE;
    _deviceState = DEVICE_STATE_CONNECTED;
    return true;
}
 


  
void CPUTEMP_Device::stop(){
    
    LOGT_DEBUG("CPUTEMP_Device  stop");

    _state = INS_UNKNOWN;
    _lastQueryTime = {0,0};
    _deviceState = DEVICE_STATE_DISCONNECTED;
}

bool CPUTEMP_Device::isConnected(){
    
    return true;
}
 

bool CPUTEMP_Device::getDeviceID(string  &devID){
    
    devID = "CPUTEMP";
    return true;
}

bool CPUTEMP_Device::getValues( keyValueMap_t &results){
    
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
            
            double tempC = 0;
            uint8_t fanState = 0;
            
            if(getCPUTemp(tempC)){
                results[_resultKey_temp] = to_string(tempC);
                gettimeofday(&_lastQueryTime, NULL);
                
                if(getFanState(fanState)){
                    results[_resultKey_cooler] = to_string(fanState);
                }
                hasData = true;
           }
        }
    }
    return hasData;
}

 

bool CPUTEMP_Device::getCPUTemp(double & tempOut) {
    bool didSucceed = false;
    
    try{
        std::ifstream   ifs;
        ifs.open("/sys/class/thermal/thermal_zone0/temp", ios::in);
        if( ifs.is_open()){
            
            string val;
            ifs >> val;
            ifs.close();
            double temp = std::stod(val);
            temp = temp /1000.0;
            tempOut = temp;
            didSucceed = true;
        }
        // debug
        else {
            
//            time_t when = time(NULL);
//            tempOut =  (when % 100);
//            didSucceed = true;
        }
    }
 
    catch(std::ifstream::failure &err) {
    }
      return didSucceed;
}


bool CPUTEMP_Device::getFanState(uint8_t  &state) {
    bool didSucceed = false;
    
    try{
        std::ifstream   ifs;
        ifs.open("/sys/class/thermal/cooling_device0/cur_state", ios::in);
        if( ifs.is_open()){
            
            string val;
            ifs >> val;
            ifs.close();
            state = std::stoi(val);
    
            didSucceed = true;
        }
    }
 
    catch(std::ifstream::failure &err) {
    }
      return didSucceed;
}
