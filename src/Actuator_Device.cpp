//
//  Actuator_Device.cpp
//  pIoTServer
//
//  Created by vinnie on 2/19/25.
//

#include "Actuator_Device.hpp"
#include "LogMgr.hpp"
#include "PropValKeys.hpp"

#include "pIoTServerMgr.hpp"
#include "ServerCmdValidators.hpp"

#include <array>

constexpr string_view Driver_Version = "1.1.0 dev 0";

bool Actuator_Device::getVersion(string &str){
    str = string(Driver_Version);
    return true;
}

Actuator_Device::Actuator_Device(string devID) :Actuator_Device(devID, string()){};

Actuator_Device::Actuator_Device(string devID, string driverName){
    setDeviceID(devID, driverName);

    _state = INS_UNKNOWN;
    _goalState = INS_UNKNOWN;
    _lastReportedState = INS_UNKNOWN;
    
    _deviceState = DEVICE_STATE_UNKNOWN;

    _lastQueryTime = {0,0};
    _isSetup = false;
    _stateChanged = false;
    
    _key_extend.clear();
    _key_retract.clear();
    _actionDuration = default_actionDuration;
    
    json j = {
//         { PROP_DEVICE_MFG_URL, "https://www.sparkfun.com/products/16304"},
//         { PROP_DEVICE_MFG_PART, "SparkFun Digital Temperature Sensor - TMP10X (Qwiic)"},
     };

    setProperties(j);
  
}

Actuator_Device::~Actuator_Device(){
    stop();
 }

bool Actuator_Device::initWithSchema(deviceSchemaMap_t deviceSchema){
  
    for(const auto& [key, entry] : deviceSchema) {
        
        if(entry.units == BOOL ){
            _resultKey_action = key;
            _isSetup = true;
         }
        else  if(entry.units == ACTUATOR ){
            _resultKey_status = key;
            _isSetup = true;
         }
    }

    _deviceState = DEVICE_STATE_DISCONNECTED;
    return _isSetup;
}

bool Actuator_Device::start(){
    bool status = false;
    
    if(_deviceID.size() == 0){
        LOGT_DEBUG("Actuator_Device has no deviceID");
        return  false;
    }

    if( _deviceProperties.contains(PROP_DEVICE_PARAMS)
       && _deviceProperties[PROP_DEVICE_PARAMS].is_object()){
        json params = _deviceProperties[PROP_DEVICE_PARAMS];
        
        if(params.contains(JSON_EXTEND)
           && params[JSON_EXTEND].is_string())
            _key_extend = params[JSON_EXTEND];
        
        if(params.contains(JSON_RETRACT)
           && params[JSON_RETRACT].is_string())
            _key_retract = params[JSON_RETRACT];
        
         unsigned long duration = 0;
        if( params.contains(JSON_ARG_DURATION)){
            if(JSON_value_toUnsigned(params[JSON_ARG_DURATION], duration)){
                _actionDuration = duration;
            }
        }
    }
    
    if(_key_extend.empty()){
        LOGT_DEBUG("Actuator_Device begin called with no %s property",
                   string(JSON_EXTEND).c_str());
            return false;
    }

    if(_key_retract.empty()){
        LOGT_DEBUG("Actuator_Device begin called with no %s property",
                   string(JSON_RETRACT).c_str());
        return false;
    }
  
    _state = INS_UNKNOWN;

    if(status){
        // wait for preflight
        _deviceState = DEVICE_STATE_DISCONNECTED;
    }
    else {
 //       LOGT_ERROR("Actuator_Device(%02X) begin FAILED: %s",i2cAddr,strerror(errno));
        _deviceState = DEVICE_STATE_ERROR;
    }
     return status;
}
 

bool Actuator_Device::preflight(){
    
    bool success = false;
    
    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();
    
    pIoTServerDB::valueSchema_t schema;
    
    schema =  db->schemaForKey(_key_extend);
    if(schema.units != valueSchemaUnits_t::BOOL){
        LOGT_DEBUG("Actuator_Device %s property %s was not a BOOL",
                   string(JSON_EXTEND).c_str(),
                   _key_extend.c_str() );
        return false;
     }
   
    schema =  db->schemaForKey(_key_retract);
    if(schema.units != valueSchemaUnits_t::BOOL){
        LOGT_DEBUG("Actuator_Device %s property %s was not a BOOL",
                   string(JSON_RETRACT).c_str(),_key_retract.c_str() );
        return false;
     }
  
    keyValueMap_t   kv = {
        {_key_extend, "off" },
        {_key_retract, "off"}
    };
    success = pIoTServer->setValues(kv);
    
    if(success){
        _lastQueryTime = {0,0};
        _state = INS_OFF;
        _goalState = INS_OFF;
     
        _deviceState = DEVICE_STATE_CONNECTED;
        
        _running = true;
         _thread = std::thread(&Actuator_Device::actionThread, this);
    
        LOGT_DEBUG("Actuator_Device(%s) ready", _deviceID.c_str()) ;
    }
 
    return true;
}


  
void Actuator_Device::stop(){
    
  //  LOGT_DEBUG("Actuator_Device(%02X) stop", _device.getDevAddr());

    _state = INS_UNKNOWN;
    _lastQueryTime = {0,0};

    _deviceState = DEVICE_STATE_DISCONNECTED;
    
  
    _running = false;
    
    {
        std::lock_guard<std::mutex> lock(_mtx);
        _goalState = INS_OFF;
        _stateChanged = true;
    }
    _cv.notify_one();

    
    // wait for action thread to complete
    while(!_thread.joinable()){
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
     }
    _thread.join();

}

bool Actuator_Device::setEnabled(bool enable){
   
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


bool Actuator_Device::isConnected(){
    return _deviceState == DEVICE_STATE_CONNECTED;
}

 

bool Actuator_Device::getValues( keyValueMap_t &results){
    
    bool hasData = false;
    
    if(!isConnected()) {
        return false;
    }
 
    if(_state != _lastReportedState){
        results = {
            { _resultKey_status,  to_string(_state)  }
        };
//        cout <<    "REPORT " << displayStringForState(_state) <<endl;

        _lastReportedState = _state;
        hasData = true;
        

    }
    return hasData;
}


bool Actuator_Device::setValues(keyValueMap_t kv){
    
    if(!isConnected())
        return false;
    
    bool success = false;
    
    if(kv.count(_resultKey_action)){
        
        bool extend = false;
        bool valid = false;
        
        string str = kv[_resultKey_action];
        if(stringToBool(str,extend)){
            valid = true;
        }
        else {
            map <string, bool> cm = {
                {"extend", true},
                {"retract",false}
            };
            
            if(cm.contains(str)){
                extend = cm[str];
                valid = true;
            }
        }
        
        if(valid){
            // only touch _state and targetState under mutex
              std::lock_guard<std::mutex> lock(_stateMutex);
  
            bool shouldMove = false;
            
            switch (_state) {
                case INS_UNKNOWN:
                case INS_OFF:
                    shouldMove = true;
                    break;
                    
                case INS_EXTENDED:
                case INS_EXTENDING:
                    if(!extend) shouldMove = true;
                    break;
                    
                case INS_RETRACTED:
                case INS_RETRACTING:
                    if(extend) shouldMove = true;
                    break;
                    
                default:
                    break;
            }
            
            if(!shouldMove)
                success = true;
            else {
 //               printf("\n-- SHOULD MOVE --\n");
                
                if(extend)
                    _goalState = INS_EXTENDED;
                else
                    _goalState = INS_RETRACTED;
                {
                    std::lock_guard<std::mutex> lock(_mtx);
                    _stateChanged = true;
                }
                _cv.notify_one();
                return true;
            }
        }
    }
    
    return success;
}

 



// thread tht handles device movement
void Actuator_Device::actionThread(){
    
    auto pIoTServer = pIoTServerMgr::shared();
    time_t lastActionStarted = MAX_TIME;

    bool moving = false;
    
    while(_running){
 
        if(moving){
            /* sleep for a little */
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        else
        {
            std::unique_lock<std::mutex> lock(_mtx);
            _stateChanged = false;
            _cv.wait(lock, [this]{ return _stateChanged; }); // Wait until ready is true
        }
 
        bool shouldMove = false;
        keyValueMap_t   kv;
        
        {  /* BEGIN MUTEX */
          std::lock_guard<std::mutex> lock(_stateMutex);
            
            /*
             GOAL            CURRENT             shouldMove
             
             INS_RETRACTED   INS_RETRACTED       F
             INS_RETRACTING
             
             INS_EXTENDED    INS_EXTENDED        F
             INS_EXTENDING
             
             all other cases  move to Goal
             
             */
            
            shouldMove = false;
            
            if( _goalState == INS_RETRACTED ){
                if( _state != INS_RETRACTED && _state != INS_RETRACTING)
                    shouldMove = true;
            }
            else  if( _goalState == INS_EXTENDED ){
                if( _state != INS_EXTENDED  && _state != INS_EXTENDING)
                    shouldMove = true;
            }
           
        } /* END MUTEX */
        
        /* create movement*/
        if(shouldMove){
            
            keyValueMap_t   kv;
            
            if(_goalState == INS_EXTENDED){
                kv = {
                    {_key_extend, "on" },
                    {_key_retract, "off"}
                };
                moving = true;
                _state = INS_EXTENDING;
 //     printf("\n-- EXTENDING --\n");
  
            }
            else if(_goalState == INS_RETRACTED){
                kv = {
                    {_key_extend, "off" },
                    {_key_retract, "on"}
                };
                moving = true;
                _state = INS_RETRACTING;
 //     printf("\n-- RETRACTING --\n");
  
            }
            else{
                kv = {
                    {_key_extend, "off" },
                    {_key_retract, "off"}
                };
                moving = false;
  
 //        printf("\n-- INVALID SHUT OFF --\n");
              }
            
            if(kv.size()){
                pIoTServer->setValues(kv);
                lastActionStarted = time(NULL);
              }
        }
        
        /* Do this when action has completed */
        if(lastActionStarted  != MAX_TIME){
            time_t now = time(NULL);
            
            if(lastActionStarted + _actionDuration <  now ){
                 
                keyValueMap_t  kv = {
                    {_key_extend, "off" },
                    {_key_retract, "off"}
                };
                moving = false;
  
                
  //   printf("\n-- SHUT OFF --\n");
                pIoTServer->setValues(kv);
                lastActionStarted = MAX_TIME;
                _state = _goalState;
   
            }
        }
       }
}


string Actuator_Device::displayStringForState(in_state_t state){
    
    const std::array<std::string, 6> stateNames = {
        "Unknown",
        "Off",
        "Extended",
        "Extending",
        "Retracting",
        "Retracted"
     };
    
    return stateNames.at(static_cast<int>(state));
}
