//
//  W1_Device.cpp
//  pIoTServer
//
//  Created by vinnie on 12/29/24.
//

#include "W1_Device.hpp"
#include "TimeStamp.hpp"
#include "LogMgr.hpp"
#include "PropValKeys.hpp"

#include <iostream>
#include <filesystem>
#include <sys/stat.h>
#include <string_view>
#include <string>



constexpr string_view Driver_Version = "1.1.0 dev 0";

bool W1_Device::getVersion(string &str){
    str = Driver_Version;
    return true;
}

W1_Device::W1_Device(string devID) :W1_Device(devID, string()){};


W1_Device::W1_Device(string devID, string driverName){
    setDeviceID(devID, driverName);
    
    _state = INS_UNKNOWN;
    _lastQueryTime = {0,0};
    _isSetup = false;

    json j = {
        { PROP_DEVICE_MFG_URL,
            {
                "http://www.hiletgo.com/ProductDetail/2169579.html",
                "https://www.raspberrypi.com/documentation/computers/configuration.html#one-wire",
                "https://www.analog.com/media/en/technical-documentation/data-sheets/ds18b20.pdf"
                "https://www.vinthewrench.com/p/picar-raspberry-pi-car-radio-project-132"
  
            } },
        { PROP_DEVICE_MFG_PART, " DS18B20 Temperature Sensor Temperature Probe Stainless Steel Package Waterproof"},
    };
    
    setProperties(j);
    _deviceState = DEVICE_STATE_UNKNOWN;
}

W1_Device::~W1_Device(){
    stop();
 }


bool W1_Device::initWithSchema(deviceSchemaMap_t deviceSchema){
    
      for(const auto& [key, entry] : deviceSchema) {
          if(entry.units == DEGREES_C ){
              _resultKey_temp = key;
              _queryDelay = entry.queryDelay != UINT64_MAX? entry.queryDelay : default_queryDelay;
              _isSetup = true;
               return true;
          }
      }
      
      return false;
  }
 
bool W1_Device::start(){
  
    if(!_deviceProperties[PROP_ADDRESS].is_string()){
        LOGT_DEBUG("W1_Device begin called with no %s property",string(PROP_ADDRESS).c_str());;
        return false;
    }
    
     _pathName =  _deviceProperties[PROP_ADDRESS];
    _usingOWFS = _pathName.find("/mnt/1wire") != std::string::npos;

    
    if(!_isSetup){
        LOGT_DEBUG("W1_Device(%s) begin called before initWithKey ",_deviceID.c_str());
        return  false;
    }
 
    if(_deviceID.size() == 0){
        LOGT_DEBUG("W1_Device has no deviceID");
        return  false;
    }

    LOGT_DEBUG("W1_Device(%s) begin %s",_deviceID.c_str(), _resultKey_temp.c_str());
  
    _lastQueryTime = {0,0};
    _state = INS_IDLE;
    _deviceState = DEVICE_STATE_CONNECTED;
    return true;
}
 
  
void W1_Device::stop(){
    
    LOGT_DEBUG("W1_Device  stop");

    _state = INS_UNKNOWN;
    _deviceState = DEVICE_STATE_DISCONNECTED;

    _lastQueryTime = {0,0};
}


bool W1_Device::setEnabled(bool enable){
   
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

bool W1_Device::isConnected(){
    
    return true;
}
 

bool W1_Device::getValues( keyValueMap_t &results){
    
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
            if(processDS18B20(tempC)){
                results[_resultKey_temp] = to_string(tempC);
                gettimeofday(&_lastQueryTime, NULL);
                hasData = true;
           }
        }
    }
    return hasData;
}

 
bool W1_Device::processDS18B20(double & tempOut) {
    bool didSucceed = false;
    
    try{
        std::ifstream   ifs;
        std:filesystem::path path = _pathName + "/temperature";
    
        ifs.open(path , ios::in);
        if( ifs.is_open()){
            
            string val;
            ifs >> val;
            ifs.close();
            double temp = std::stod(val);
            
    /*
     STUPID STUPID LINUX 1WIRE DRIVER
                
     reading from  /sys/bus/w1/devices/<device> /temperature  will give you an integer  like 16312
     reading from /mnt/1wire/<device>/temperature    will give you value like 16.312 or even 16
     we really need to write our own I2C bridge.
     
     
     */
            if(!_usingOWFS){
                temp = temp /1000.0;
            }
            
            //  The power-on reset value of the temperature register is +85°C.
            
            if(temp < 85.0){
                tempOut = temp;
                didSucceed = true;
           }
         }
    }
 
    catch(std::ifstream::failure &err) {
    
    }
    
    
    return didSucceed;
}

// MARK: -   W1 tool

 
bool W1_Device::getW1Devices(std::vector<std::string> &names){
    
    /*
     cat /sys/bus/w1/devices/28-00000035943e/temperature
     15312

     cat  /mnt/1wire/28.793434000000/temperature
     21.937
     cat  /mnt/1wire/28.793434000000/temperature
     22
     
     cat  /mnt/1wire/28.B9F533000000/temperature
     20.875
     */
    string path = "/sys/bus/w1/devices/";
    names.clear();
    try
    {
        for (const auto & entry : std::filesystem::directory_iterator(path)){
            
            string filename = entry.path().filename();
            
            if(filename.starts_with("w1_bus_master")) continue;
            if(entry.is_symlink()){
                 names.push_back( filename);
            }
        }
    }
    catch (std::filesystem::filesystem_error const& ex)
    {
        return false;
    }
     return true;
}
