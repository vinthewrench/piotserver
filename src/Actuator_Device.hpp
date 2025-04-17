//
//  Actuator_Device.hpp
//  pIoTServer
//
//  Created by vinnie on 2/19/25.
//

#ifndef Actuator_Device_hpp
#define Actuator_Device_hpp

#include "pIoTServerDevice.hpp"
#include <thread>            //Needed for std::thread
#include <mutex>
#include <condition_variable>

using namespace std;

class Actuator_Device : public pIoTServerDevice{
    
public:
    
    typedef enum  {
        INS_UNKNOWN = 0,
        INS_OFF ,
        INS_EXTENDED,
        INS_EXTENDING,
        INS_RETRACTING,
        INS_RETRACTED,
    }in_state_t;
    
    static string displayStringForState(in_state_t state);
    
    constexpr static string_view JSON_EXTEND   = "extend";
    constexpr static string_view JSON_RETRACT   = "retract";
  
    Actuator_Device(string devID, string driverName);
    Actuator_Device(string devID);
    ~Actuator_Device();
    bool getVersion(string  &version);

    bool initWithSchema(deviceSchemaMap_t deviceSchema);
    
    bool start();
    void stop();
    bool preflight();
    
    bool isConnected();
    bool setEnabled(bool enable);
    
    bool getValues( keyValueMap_t &);
    bool setValues(keyValueMap_t kv);
    
private:
    static const uint64_t default_actionDuration = 8;
    bool                _isSetup = false;
    timeval            _lastQueryTime;
    
    string          _resultKey_status;
    string          _resultKey_action;
    in_state_t      _lastReportedState = INS_UNKNOWN;
    
    
    string      _key_extend;
    string      _key_retract;
    uint64_t    _actionDuration;
    
    
    void                    actionThread();
    std::thread             _thread;                //Internal thread
    mutable std::mutex      _stateMutex;
    in_state_t              _goalState;
    in_state_t              _state;
    
    bool                    _running;                //Flag for starting and terminating the main loop
    
    
    std::condition_variable _cv;
    std::mutex              _mtx;
    bool                    _stateChanged;
    
    
};

#endif /* Actuator_Device_hpp */
