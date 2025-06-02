//
//  SAMPLE_Device.cpp
//  pIoTServer
//
//  Created by vinnie on 1/9/25.
//

#include "SAMPLE_Device.hpp"
#include "PropValKeys.hpp"

SAMPLE_Device::SAMPLE_Device(string devID, string driverName){

    setDeviceID(devID, driverName);
 
    _deviceState = DEVICE_STATE_UNKNOWN;
    _isSetup = false;
}

SAMPLE_Device::~SAMPLE_Device(){
    stop();
}


bool SAMPLE_Device::initWithSchema(deviceSchemaMap_t deviceSchema){
    
    for(const auto& [key, entry] : deviceSchema) {
        if(entry.units == SECONDS ){
            _resultKey = key;
            _isSetup = true;
            return true;
        }
    }
    
    _deviceState = DEVICE_STATE_DISCONNECTED;
    return false;
}



bool SAMPLE_Device::start(){
    _startup_time = time(NULL);
    _deviceState = DEVICE_STATE_CONNECTED;
    return true;
}




void SAMPLE_Device::stop(){
    _startup_time = 0;
    _deviceState = DEVICE_STATE_DISCONNECTED;
    
}

bool SAMPLE_Device::setEnabled(bool enable){
    
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


bool SAMPLE_Device::isConnected(){
    return true;
}

bool SAMPLE_Device::getValues (keyValueMap_t &results){
    
    if(!isConnected())
        return false;
    
    time_t now = time(NULL);
    results[_resultKey] = to_string(now - _startup_time);
    return true;
}
