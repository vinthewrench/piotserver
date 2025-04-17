//
//  Sprinkler_Device.cpp
//  pIoTServer
//
//  Created by vinnie on 4/9/25.
//

#include "Sprinkler_Device.hpp"
#include "LogMgr.hpp"
#include "PropValKeys.hpp"

#include "pIoTServerMgr.hpp"
#include "ServerCmdValidators.hpp"

#include <array>

constexpr string_view JSON_ARG_PROXY                = "proxy_key";
 
void Sprinkler_Device::clearCmdQueue()
{
   std::queue<cmd_t> empty;
    std::swap( _cmdQueue, empty );
}

constexpr string_view Driver_Version = "1.1.0 dev 0";

bool Sprinkler_Device::getVersion(string &str){
    str = Driver_Version;
    return true;
}

Sprinkler_Device::Sprinkler_Device(string devID) :Sprinkler_Device(devID, string()){};

Sprinkler_Device::Sprinkler_Device(string devID, string driverName){
    setDeviceID(devID, driverName);
  
    _lastReportedTag = _stateTag = 0;
 
    _deviceState = DEVICE_STATE_UNKNOWN;

    _isSetup = false;
    _goalChanged = false;

    _proxyMap.clear();
    clearCmdQueue();
    
    _boosterRelay = {};
    _masterRelay = {};

    json j = {
//         { PROP_DEVICE_MFG_URL, "https://www.sparkfun.com/products/16304"},
//         { PROP_DEVICE_MFG_PART, "SparkFun Digital Temperature Sensor - TMP10X (Qwiic)"},
     };

    setProperties(j);
  
}

Sprinkler_Device::~Sprinkler_Device(){
    stop();
 }

bool Sprinkler_Device::initWithSchema(deviceSchemaMap_t deviceSchema){
  
    _proxyMap.clear();
    _boosterRelay = {};
    _masterRelay = {};
    clearCmdQueue();
   _boosterDuration = 0;
  
    _runUpDuration = 2;
    _runDownDuration = 2;
 
    for(const auto& [key, entry] : deviceSchema) {
        if(!entry.otherProps.is_null() ){
            json j = entry.otherProps;
            
            if(j.count(JSON_ARG_PROXY) && j[JSON_ARG_PROXY].is_string()){
                string proxy = j[JSON_ARG_PROXY];
                 
                if(entry.units == BOOSTER) {
                    _boosterRelay.name = key;
                    _boosterRelay.proxyName = proxy;
                    _boosterRelay.state = false;
                    _boosterRelay.enabled = true;
                     unsigned long duration = 0;
        
                    if( j.contains(JSON_ARG_DURATION)
                       && JSON_value_toUnsigned(j[JSON_ARG_DURATION], duration)){
                        _boosterDuration = duration;
                    }
                }
                else if(entry.units == MASTER_RELAY) {
                    _masterRelay.name = key;
                    _masterRelay.proxyName = proxy;
                    _masterRelay.state = false;
                    _masterRelay.enabled = true;
                 }
                else if(entry.units == BOOL) {
                    valve_t valve = {
                        .name = key,
                        .proxyName = proxy,
                        .state = false,
                        .enabled = true
                    };
                    _proxyMap[key] = valve;
                }
            }
        }
        
        if(_proxyMap.size() > 0) {
            _isSetup = true;
        }
    }

    _deviceState = DEVICE_STATE_DISCONNECTED;
    return _isSetup;
}

bool Sprinkler_Device::start(){
    bool status = false;
    
    if(_deviceID.size() == 0){
        LOGT_DEBUG("Sprinkler_Device has no deviceID");
        return  false;
    }
    
    if(status){
        // wait for preflight
        _deviceState = DEVICE_STATE_DISCONNECTED;
    }
    else {
 //       LOGT_ERROR("Sprinkler_Device(%02X) begin FAILED: %s",i2cAddr,strerror(errno));
        _deviceState = DEVICE_STATE_ERROR;
    }
     return status;
}
 

bool Sprinkler_Device::preflight(){
    
    bool success = false;
    clearCmdQueue();
 
    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();
    
    pIoTServerDB::valueSchema_t schema;
    keyValueMap_t   kv;

    if(_boosterRelay.enabled){
        schema =  db->schemaForKey(_boosterRelay.proxyName);
        if(schema.units != valueSchemaUnits_t::BOOL){
            LOGT_DEBUG("Sprinkler_Device %s property %s was not a BOOL",
                       _boosterRelay.name.c_str(),
                       _boosterRelay.proxyName.c_str() );
            return false;
        }
        kv[_boosterRelay.proxyName] = "off";
    }
    if(_masterRelay.enabled){
        schema =  db->schemaForKey(_masterRelay.proxyName);
        if(schema.units != valueSchemaUnits_t::BOOL){
            LOGT_DEBUG("Sprinkler_Device %s property %s was not a BOOL",
                       _masterRelay.name.c_str(),
                       _masterRelay.proxyName.c_str() );
            return false;
         }
        kv[_masterRelay.proxyName] = "off";
    }
    
    for(auto [key, valve] : _proxyMap){
        if(valve.enabled){
            schema =  db->schemaForKey(valve.proxyName);
            if(schema.units != valueSchemaUnits_t::BOOL){
                LOGT_DEBUG("Sprinkler_Device %s property %s was not a BOOL",
                           valve.name.c_str(),
                           valve.proxyName.c_str() );
                return false;
            }
            kv[valve.proxyName] = "off";
        }
    }
     
    // turn them all off
    success = pIoTServer->setValues(kv);
 
    if(success){
        _stateTag++;
        _deviceState = DEVICE_STATE_CONNECTED;
        
        _running = true;
         _thread = std::thread(&Sprinkler_Device::actionThread, this);
        
        LOGT_DEBUG("Sprinkler_Device(%s) ready", _deviceID.c_str()) ;
    }
 
    return true;
}


  
void Sprinkler_Device::stop(){
    
   LOGT_DEBUG("Sprinkler_Device(%s) stop", _deviceID.c_str());
    _deviceState = DEVICE_STATE_DISCONNECTED;
    
    doShutDown();
 
    // wait for action thread to complete
    while(!_thread.joinable()){
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
     }
    _thread.join();

}

bool Sprinkler_Device::setEnabled(bool enable){
   
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


bool Sprinkler_Device::isConnected(){
    return _deviceState == DEVICE_STATE_CONNECTED;
}

 

bool Sprinkler_Device::getValues( keyValueMap_t &results){
    
    bool hasData = false;
    
    if(!isConnected()) {
        return false;
    }
 
    if(_stateTag != _lastReportedTag){
        
        std::lock_guard<std::mutex> lock(_cmdMutex);
 
         if(_boosterRelay.enabled){
             results[_boosterRelay.name] = to_string(_boosterRelay.state);
         }
        
        if(_masterRelay.enabled){
            results[_masterRelay.name] = to_string(_masterRelay.state);
        }
 
        for(auto [key, valve] : _proxyMap){
            if(valve.enabled){
                results[valve.name] = to_string(valve.state);
            }
        }
        _lastReportedTag = _stateTag;
        hasData = true;
     }
    return hasData;
}


bool Sprinkler_Device::setValues(keyValueMap_t kv){
    
    if(!isConnected())
        return false;
    
    vector<cmd_t> newCmds;
    
    // que up commands
    {
        for(auto [key, val]: kv){
            
             if(_proxyMap.count(key)){
                valve_t valve = _proxyMap[key];
                bool requestedState;
                if(stringToBool(val,requestedState)){
                    newCmds.push_back({key, requestedState});
                 }
            }
        }
    }
  
    if(newCmds.size()){
        {
            std::lock_guard<std::mutex> lock(_cmdMutex);
            for(auto cmd : newCmds){
//                cout << "PUSH " << cmd.first << " " << cmd.second << endl;
                _cmdQueue.push(cmd);
            }
        }
        {
            std::lock_guard<std::mutex> lock(_goalMtx);
            _goalChanged = true;
        }
        _cv.notify_one();
    }
    return true;
}

bool Sprinkler_Device::doShutDown(){
    
    vector<cmd_t> newCmds;
    
    // que up commands
    for(auto [key, valve]: _proxyMap){
        if(valve.enabled)
            newCmds.push_back({key, false});
    }
    {
        std::lock_guard<std::mutex> lock(_cmdMutex);
        for(auto cmd : newCmds){
//            cout << "PUSH " << cmd.first << " " << cmd.second << endl;
            _cmdQueue.push(cmd);
        }
    }
    {
        std::lock_guard<std::mutex> lock(_goalMtx);
        _goalChanged = true;
    }
    _cv.notify_one();
    
    _running = false;
    _cv.notify_one();
    
    return true;
}

void Sprinkler_Device::actionThread(){

    /*
     This is a fairly complicated state machine
     
     we have to handle the option of having a booster relay.
     which goes on before any new valve opens.  We use the booster so we can
     drive sprinkler valves with DC current. the idea is that we can use a higher voltage
     to open the valve and then run it at a lower voltage once opened.
     
     The master valve is an option that some systems use ahead of any sprinkler valves
     either as a device to prevent keeping pressure in the pipes or to pump water into
     the system -- there is a _runDownDuration specified so that we dont chatter the master
     valve inbetween  close of one and open of the next providing some form of hysterisis.
     
     */
    
    
    time_t runUpStarted = MAX_TIME;
    time_t runDownStarted = MAX_TIME;
    time_t boosterStarted = MAX_TIME;

   
    _state = INS_IDLE;
    
    while(_running){
        bool hasMasterValve = _masterRelay.enabled;
        bool hasBooster =   _boosterRelay.enabled;

        bool hasONCmd = false;
        
        bool isWaiting   = (_state == INS_RUNUP)
        || (_state == INS_RUNDOWN)
        || (_state == INS_BOOST)
        || (_state == INS_RUNDOWN);
        
        if(isWaiting){
            /* sleep for a little */
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        else
        {
            std::unique_lock<std::mutex> lock(_goalMtx);
            _cv.wait(lock, [this]{ return _goalChanged; }); // Wait until ready is true
         }
        
        time_t now = time(NULL);
        
        // is this command going to switch on a new device?
        {
            std::lock_guard<std::mutex> lock(_cmdMutex);
     
            if(!_cmdQueue.empty()){
                for(auto it =_cmdQueue.begin(); it!=_cmdQueue.end();++it){
                    cmd_t cmd =  *it;
                    string  key = cmd.first;
                    bool     newState = cmd.second;
                    
                    if(newState){
                        if(!_proxyMap.count(key)
                           || !_proxyMap[key].state){
                            hasONCmd = true;
 //                           cout  << "CMD QUEUE: " << _cmdQueue.size() <<  " has ON " << _state << endl;
                        }
                    }
                }
            }
        }
    
        
        if(_state == INS_BOOST){
            if(hasONCmd){
                // we were boosting and needed to reBoost
                //                cout << "restart Boost" << endl;
                boosterStarted = now;
                _state = INS_BOOST;
            }
            else  if(boosterStarted + _boosterDuration <  now ){
                setBoosterValve(false);
                boosterStarted = MAX_TIME;
                _state = INS_RUN;
            }
        }
        
        if(_state == INS_IDLE){
            if(hasONCmd){
                if(hasMasterValve){
                    setMasterValve(true);
                    runUpStarted = now;
                    _state = INS_RUNUP;
                }
                else if(hasBooster){
                    setBoosterValve(true);
                    boosterStarted = now;
                    _state = INS_BOOST;
                }
                else {
                    _state = INS_RUN;
                }
            }
        }
   
        if(_state == INS_RUNUP){
            if(runUpStarted + _runUpDuration <  now ){
                if(hasBooster){
                    setBoosterValve(true);
                    boosterStarted = now;
                    _state = INS_BOOST;
                }
                else {
                    _state = INS_RUN;
                }
            }
        }
        
        // we were running and needed to reBoost
        if((_state == INS_RUN)
           &&  hasONCmd
           &&  hasBooster){
            setBoosterValve(true);
            boosterStarted = now;
            _state = INS_BOOST;
        }
        
        if(_state == INS_RUNDOWN) {
            // do we abort a rundown
            if(hasONCmd){
                if(hasBooster){
                    setBoosterValve(true);
                    boosterStarted = now;
                    _state = INS_BOOST;
                }
                else {
                    _state = INS_RUN;
                }
                runDownStarted = MAX_TIME;
            }
            else {
                if(runDownStarted + _runDownDuration <  now ){
                    setMasterValve(false);
                    runDownStarted = MAX_TIME;
                    _state = INS_IDLE;
 //                                      cout << "IDLE" << endl;
                }
            }
        }
        if(_state == INS_BOOST
           || _state == INS_RUN){
            
            std::lock_guard<std::mutex> lock(_cmdMutex);
    
            while (!_cmdQueue.empty()) {
                
  //              cout << "CMD QUEUE: " << _cmdQueue.size() << endl;
                auto cmd = _cmdQueue.front() ;
                string  key = cmd.first;
                bool    newState = cmd.second;
                
                // are we changing existing valve states
                if(!_proxyMap.count(key)
                   || ( _proxyMap[key].state  != newState)){
                    setProxyValve(key, newState);
                }
                
                _cmdQueue.pop();
            }
            
            // are there any valves open
            bool valvesOpen = false;
            for(auto valve : _proxyMap){
                if(valve.second.state) valvesOpen = true;
            }
            
            if(!valvesOpen){
                // did we shutdown durring a boost
                if(_state == INS_BOOST){
                    setBoosterValve(false);
                    boosterStarted = MAX_TIME;
                }
                
                if( hasMasterValve){
                    _state = INS_RUNDOWN;
//                                       cout << "Start RunDown" << endl;
                    runDownStarted = now;
                }
                else {
                    _state = INS_IDLE;
//                                       cout << "IDLE" << endl;
                }
            }
        }
    }
    
}


void Sprinkler_Device::setBoosterValve(bool state){
    
    if(_boosterRelay.enabled){
   //     cout << "Turn " << (state?"ON ":"OFF") <<  " Booster" << endl;
       auto pIoTServer = pIoTServerMgr::shared();
        pIoTServer->setValues({{_boosterRelay.proxyName, to_string(state)}});
        _boosterRelay.state = state;
       _stateTag++;
   }
 }

void Sprinkler_Device::setMasterValve(bool state){
    
    
    if(_masterRelay.enabled){
  //      cout << "Turn " << (state?"ON ":"OFF") <<  " Master" << endl;
        auto pIoTServer = pIoTServerMgr::shared();
        pIoTServer->setValues({{_masterRelay.proxyName, to_string(state)}});
        _masterRelay.state = state;
        _stateTag++;
   }
 }

void Sprinkler_Device::setProxyValve(string key, bool state){
    if(_proxyMap.count(key) && _proxyMap[key].enabled){
 //       cout << "Turn " << (state?"ON ":"OFF") <<  " " << key << endl;
        auto pIoTServer = pIoTServerMgr::shared();
        pIoTServer->setValues({{_proxyMap[key].proxyName, to_string(state)}});
        _proxyMap[key].state = state;
        _stateTag++;
       }
 }

  /*
// thread tht handles device movement
void Sprinkler_Device::actionThread(){
    
    // the master_hysterisis is there to prevent chattering a relay
    // when you have on command immediately followed by an off in a sequence..
    
    static const uint64_t master_hysterisis = 3;
 
    auto pIoTServer = pIoTServerMgr::shared();
    time_t boosterStarted = MAX_TIME;
    time_t masterStopped = MAX_TIME;
  
    bool boosting = false;
    bool masterDelay = false;
    
    while(_running){
        
        boolMap_t targetStates;
        
        if(boosting || masterDelay){
             std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        else
        {
            std::unique_lock<std::mutex> lock(_goalMtx);
            _goalChanged = false;
            //            cout << "sleep " << endl;
            _cv.wait(lock, [this]{ return _goalChanged; }); // Wait until ready is true
        }
        
        keyValueMap_t   kv;
        
        {
            // walk the cmd queue looking for keys that need to be turned on or off
            std::lock_guard<std::mutex> lock(_cmdMutex);
            while (!_cmdQueue.empty()) {
                auto cmd = _cmdQueue.front() ;
                string  key = cmd.first;
                bool    newState = cmd.second;
                
                //               cout << "POP " << key << " " << newState << endl;
                
                // are we turning on anything
                if(newState){
                    // do we need to turn on master valve
                    if(_masterRelay.enabled && !_masterRelay.state){
                        pIoTServer->setValues({{_masterRelay.proxyName, to_string(true)}});
                        _masterRelay.state = true;
                        
//                        LOGT_DEBUG("Master On" );
                    }
                    
                    // do we need to run a booster
                    if(_boosterRelay.enabled){
                        if(!_boosterRelay.state){
                            pIoTServer->setValues({{_boosterRelay.proxyName, to_string(true)}});
                            _boosterRelay.state = true;
                            boosting = true;
                            _stateTag++;
//                            LOGT_DEBUG("Booster On" );
                        }
                        
                        // restart the clock
                        boosterStarted = time(NULL);
                    }
                }
                
                // fill the map of proxy keys that need changes
                valve_t valve = _proxyMap[key];
                kv[valve.proxyName] = to_string(newState);
                _proxyMap[key].state = newState;
  //              LOGT_DEBUG("%s / %s  =  %d" ,key.c_str(), valve.proxyName.c_str(), newState );
                _cmdQueue.pop();
            }
            
        }
        
        if(kv.size()){
            pIoTServer->setValues(kv);
            _stateTag++;
        }
        
        // are we in a booster delay?
        if(boosterStarted  != MAX_TIME){
            time_t now = time(NULL);
            
            if(boosterStarted + _boosterDuration <  now ){
                // shut off booster
                pIoTServer->setValues({{_boosterRelay.proxyName, to_string(false)}});
                _boosterRelay.state = false;
                boosting = false;
                boosterStarted = MAX_TIME;
                _stateTag++;
//                LOGT_DEBUG("Booster Off" );
            }
        }
        
        // do we need to shut off master valve?
        if(_masterRelay.enabled && _masterRelay.state){
            bool valvesOn = false;
            for(auto [key, valve]: _proxyMap){
                if(valve.state) valvesOn = true;
            }
            
            // we give the master valve a few seconds of hysterisis before we turn it off
            //in case another turn on happens.
            
            if(valvesOn){
                // reset any master delay
                masterStopped = MAX_TIME;
                masterDelay = false;
            }
            else  {
                // prepare to shut off master
                 time_t now = time(NULL);
                
                if(masterStopped == MAX_TIME){
                    masterStopped = now;
                    masterDelay = true;
                }
                else  if(masterStopped + master_hysterisis <  now ){
                    masterStopped = MAX_TIME;
                    masterDelay = false;
                    
                    // shut off master
                    pIoTServer->setValues({{_masterRelay.proxyName, to_string(false)}});
                    _masterRelay.state = false;
//                    LOGT_DEBUG("Master Off" );
                    
                    // shut off booster if it was running
                    if(boosterStarted != MAX_TIME){
                        pIoTServer->setValues({{_boosterRelay.proxyName, to_string(false)}});
                        _boosterRelay.state = false;
                        boosting = false;
                        boosterStarted = MAX_TIME;
//                        LOGT_DEBUG("Booster Off" );
                    }
                    _stateTag++;
                }
            }
        }
    }
}

*/
