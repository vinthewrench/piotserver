//
//  Sprinkler_Device.hpp
//  pIoTServer
//
//  Created by vinnie on 4/9/25.
//

#ifndef Sprinkler_Device_hpp
#define Sprinkler_Device_hpp

#include "pIoTServerDevice.hpp"
#include <thread>            //Needed for std::thread
#include <mutex>
#include <queue>
#include <deque>

#include <condition_variable>
 
 
template<typename T, typename Container=std::deque<T> >
class iterable_queue : public std::queue<T,Container>
{
public:
    typedef typename Container::iterator iterator;
    typedef typename Container::const_iterator const_iterator;

    iterator begin() { return this->c.begin(); }
    iterator end() { return this->c.end(); }
    const_iterator begin() const { return this->c.begin(); }
    const_iterator end() const { return this->c.end(); }
};

class Sprinkler_Device : public pIoTServerDevice{
 
public:
  
    Sprinkler_Device(string devID, string driverName);
    Sprinkler_Device(string devID);
    ~Sprinkler_Device();
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
 
    typedef enum  {
        INS_UNKNOWN = 0,
        INS_IDLE ,
        INS_RUNUP,
        INS_RUN,
        INS_BOOST,
        INS_RUNDOWN,
    }in_state_t;

    bool doShutDown();
    
    void clearCmdQueue();
  
    void setBoosterValve(bool state);
    void setMasterValve(bool state);
    void setProxyValve(string key, bool state);
 
    bool                _isSetup = false;
    in_state_t          _state = INS_UNKNOWN;

    typedef struct {
        string      name;
        string      proxyName;
        bool        state;
        bool        enabled;
    } valve_t;

    
    typedef pair<string, bool> cmd_t;       // valve cmd
     map<string, valve_t>  _proxyMap;

    valve_t               _masterRelay;
    valve_t               _boosterRelay;
    uint64_t              _boosterDuration;
    uint64_t              _runUpDuration;
    uint64_t              _runDownDuration;
   
    void                    actionThread();
    std::thread             _thread;                //Internal thread
     
    eTag_t                  _stateTag;
    eTag_t                  _lastReportedTag;
 
    bool                    _running;                //Flag for starting and terminating the main loop
  
    mutable std::mutex      _cmdMutex;

    mutable std::mutex      _goalMtx;
    std::condition_variable _cv;

    iterable_queue<cmd_t>       _cmdQueue;
    bool                        _goalChanged;
};

#endif /* Sprinkler_Device_hpp */
