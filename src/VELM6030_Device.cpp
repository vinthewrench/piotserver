//
//  VELM6030_Device.cpp
//  pIoTServer
//
//  Created by vinnie on 1/21/25.
//

#include "VELM6030_Device.hpp"
#include "TimeStamp.hpp"
#include "LogMgr.hpp"
#include "PropValKeys.hpp"
#include "croncpp.hpp"
#include "EventAction.hpp"


constexpr string_view Driver_Version = "1.1.0 dev 0";

bool VELM6030_Device::getVersion(string &str){
    str = Driver_Version;
    return true;
}

VELM6030_Device::VELM6030_Device(string devID) :VELM6030_Device(devID, string()){};

VELM6030_Device::VELM6030_Device(string devID, string driverName){
    setDeviceID(devID, driverName);

    _state = INS_UNKNOWN;
    _deviceState = DEVICE_STATE_UNKNOWN;

    _lastQueryTime = {0,0};
    _isSetup = false;
    
    json j = {
        { PROP_DEVICE_MFG_URL,
            {
                "https://www.sparkfun.com/sparkfun-ambient-light-sensor-veml6030-qwiic.html",
                "https://learn.adafruit.com/adafruit-veml7700/overview",
               "https://www.vishay.com/docs/84323/designingveml7700.pdf",
                "https://vishay.com/docs/84367/designingveml6030.pdf"
            }
        },
         { PROP_DEVICE_MFG_PART, "High Accuracy Ambient Light Sensor - VEML6030/VEML7700 (Qwiic)"},
     };

    setProperties(j);
  
}

VELM6030_Device::~VELM6030_Device(){
    stop();
 }

bool VELM6030_Device::initWithSchema(deviceSchemaMap_t deviceSchema){
    
    for(const auto& [key, entry] : deviceSchema) {
        if(entry.units == LUX ){
            _resultKey_lux = key;
            _queryDelay = entry.queryDelay != UINT64_MAX? entry.queryDelay : default_queryDelay;
            _isSetup = true;
            return true;
        }
    }
    _deviceState = DEVICE_STATE_DISCONNECTED;
    
    return false;
}

bool VELM6030_Device::start(){
    bool status = false;
    int error = 0;
    
    if(!_deviceProperties[PROP_ADDRESS].is_string()){
        LOGT_DEBUG("VELM6030_Device begin called with no %s property",string(PROP_ADDRESS).c_str());;
        return false;
    }
    
    if(_deviceID.size() == 0){
        LOGT_DEBUG("VELM6030_Device has no deviceID");
        return  false;
    }

    string address  = _deviceProperties[PROP_ADDRESS];
    uint8_t i2cAddr = std::stoi(address.c_str(), 0, 16);
  
    if(!_isSetup){
        LOGT_DEBUG("VELM6030_Device(%s) begin called before initWithKey ",address.c_str());
        return  false;
    }
 
    LOGT_DEBUG("VELM6030_Device(%02X) begin %s",i2cAddr, _resultKey_lux.c_str());
    status = _device.begin(i2cAddr, error);

    if(status){
        _fcAvg = 0;
        _fvAvgAvail = false;
        
        _lastQueryTime = {0,0};
        _state = INS_IDLE;
        _deviceState = DEVICE_STATE_CONNECTED;
    }
    else {
        LOGT_ERROR("VELM6030_Device(%02X) begin FAILED: %s",i2cAddr,strerror(errno));
       _state = INS_INVALID;
        _deviceState = DEVICE_STATE_ERROR;
    }
     return status;
}
 


  
void VELM6030_Device::stop(){
    
    LOGT_DEBUG("VELM6030_Device(%02X) stop", _device.getDevAddr());

    _state = INS_UNKNOWN;
    _lastQueryTime = {0,0};

    if(_device.isOpen()){
        _device.stop();
    }
    _deviceState = DEVICE_STATE_DISCONNECTED;
}

bool VELM6030_Device::setEnabled(bool enable){
   
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


bool VELM6030_Device::isConnected(){
      return _device.isOpen();
}
 

bool VELM6030_Device::getValues( keyValueMap_t &results){
    
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
            uint32_t  lux = 0;
            uint32_t  wLux = 0;
  
            if(_fvAvgAvail){
                
                 /*
                 Convert foot-candles per hour to PAR (µmol.m-2.s-1) based on light
                 source.  Sunlight has 0.20 foot-candles per µmol.m-2.s-1.
                */
                double PAR = _fcAvg * 0.2;
                
//                /*
//                Convert PAR to DLI. PAR (µmol. m-2.s-1) x 0.0864
//                The 0.0864 factor is the total number of seconds in a day divided by
//                1,000,000
//                */
//                double DLI = PAR * 0.0864;
//                
                string result_PAR = _resultKey_lux+ ".PAR";
                results[result_PAR] = to_string(PAR);
                
                _fvAvgAvail = false;
                _fcAvg = 0;
  
                 hasData = true;
            }
            
            if(_device.readLight(lux)){
                
                results[_resultKey_lux] = to_string(lux);
                
                 //  lux to foot candles  1 lx = 0.09290304  fc
                double fc = lux * 0.09290304;
                _fcAvg = (fc + lux) /2 ;
                
                hasData = true;
            }
        
            if(_device.readWhiteLight(wLux)){
                
                string result_WhiteLux = _resultKey_lux+ ".WHITE";
                results[result_WhiteLux] = to_string(wLux);
                 hasData = true;
            }
            
   
            if(hasData){
                gettimeofday(&_lastQueryTime, NULL);
           }
         }
    }
    return hasData;
}


void VELM6030_Device::eventNotification(EventTrigger trig){
    
    if(trig.isCronEvent()){
        static time_t nextCronEvent = 0;
        time_t now = time(NULL);
      
        if(nextCronEvent <= now){
            
            // skip first one
            if(nextCronEvent != 0)  _fvAvgAvail = true;
            
            auto cron = cron::make_cron(preprocess_cronstring("@hourly"));
            nextCronEvent = cron::cron_next(cron, now);
        }
    }
    
      else if(trig.isAppEvent()){
        printf(" VELM6030_Device app event  %s\n", trig.printString().c_str());
     }
}

/*
 lux to foot candles
 
 1 lx = 0.09290304 ft*c, fc
 
 Determine the average number of foot-candles per hour. Take the  hourly foot-candle averages for the day, add them, and then divide this  sum by 24.
 
 For example, you have 24 hourly foot-candle readings:
 0 + 0 + 0 + 0 + 0 + 5 + 12 + 21 + 40 + 43 + 159 + 399 + 302 +
 461 + 610 + 819 + 567 + 434 + 327 + 264 + 126 + 15 + 4 + 0 =
 4,408 foot-candles
 4,408 foot-candles ÷ 24 hours = 184 foot-candles per hour
 
 
 Convert foot-candles per hour to
 PAR (µmol.m-2.s-1) based on light
 source. Do this by multiplying footcandles per hour by a factor for the
 light source.
 Sunlight has 0.20 foot-candles per
 µmol.m-2.s-1. HPS lamps have 0.13
 foot-candles per µmol.m-2.s-1.
 
 Using the same example as above, the PAR for crops receiving natural sunlight would be calculated like this:
 184 foot-candles per hour x 0.20 foot-candles per µmol.m-2.s-1
 = 36.8 µmol.m-2.s-1
 For HPS lamps, the PAR would be:
 184 foot-candles per hour x 0.13 foot-candles per µmol.m-2.s-1
 = 23.9 µmol.m-2.s
 
 
 Convert PAR to DLI. Do this by
 using the following equation:
 PAR (µmol.
 m-2.s-1) x 0.0864
 The 0.0864 factor is the total number of seconds in a day divided by
 1,000,000
 
 
 For crops receiving natural sunlight:
 36.8 µmol.m-2.s-1 x 0.0864 = 3.2 mol.
 m-2.d-1
 For crops receiving HPS lighting:
 23.9 µmol.m-2.s-1 x 0.0864 = 2.1 mol.
 m-2.d-1
 
 
 */
