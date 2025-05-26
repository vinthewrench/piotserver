//
//  pIoTServerMgr.hpp
//  pIoTServer
//
//  Created by vinnie on 12/2/24.
//

#ifndef pIoTServerMgr_hpp
#define pIoTServerMgr_hpp

#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <thread>            //Needed for std::thread
#include <functional>
#include <cstdlib>
#include <signal.h>
#include <random>

#include <condition_variable>
#include <unordered_set>

#include "pIoTServerDB.hpp"
#include "SolarTimeMgr.hpp"

#include "pIoTServerDevice.hpp"

using namespace std;


// pIoTServerMgrDevice is a psuedo device that represents the server itself
// mostly we use it to hold global variables.

class pIoTServerMgrDevice : public pIoTServerDevice{
    
public:
 
    static const uint64_t default_queryDelay = 5;
  
    pIoTServerMgrDevice();
    ~pIoTServerMgrDevice();
    
    bool initWithSchema(deviceSchemaMap_t deviceSchema);

    bool start();
    void stop();
    
    bool isConnected();
    bool setEnabled(bool enable);
    bool getDeviceID(string  &devID);       // this is a constant value
    bool setValues(keyValueMap_t kv);
    bool setInitialValues(keyValueMap_t kv);
  
    bool getValues( keyValueMap_t &);
    
    void getProperties(json  &j);
    
    bool getBuiltInSchemas(  deviceSchemaMap_t &schemas );

private:
    
    bool        _isSetup = false;

    typedef enum  {
        INS_UNKNOWN = 0,
        INS_IDLE ,
        INS_INVALID,
        INS_RESPONSE,  // we DONt use it
     }in_state_t;
    
    in_state_t         _state;
     timeval            _lastQueryTime;
    uint64_t            _queryDelay;            // how long to wait before next query
    
    solarTimes_t        _cachedSolarTimes;
    
    typedef struct globalVarEntry_t {
        string              title;
        bool                readOnly    = false;
        bool                isEquation  = false; 
        valueSchemaUnits_t  units       = UNKNOWN;
     } globalVarEntry_t;

   std::map<std::string, globalVarEntry_t> _globalValues;
 
    typedef struct formulaEntry_t {
        string         formula;
        string          lastResult;
       } formulaEntry_t;

    std::map<std::string, formulaEntry_t> _formulas;
    eTag_t  _lastEtag = MAX_ETAG;
    
    bool getCPUTemp(double & tempOut);
    bool getFanState(uint8_t  &state);

};

class pIoTServerMgr {
 
public:
    static const char*     pIoTServerMgr_Version;
    
    static pIoTServerMgr *shared() {
        if(!_sharedInstance){
            _sharedInstance = new pIoTServerMgr;
        }
        return _sharedInstance;
    }
    
    typedef  string piSensorKey_t;

    pIoTServerMgr();
    ~pIoTServerMgr();
    
    void setAssetDirectoryPath(string filePath);
    void setPropFileName(string name);
    
    void start();
    void stop();
    bool isRunning() {return _running; };
  
    void startDevices();
    bool loadGlobalValues();
    bool stopDevices();
    bool readDevices();
    void setupDeviceNotifications();
    
    void getAllDeviceStatus(json &j);

    void getAllDeviceProperties(json &j);
    bool getDeviceProperties(string deviceID, json &j);
    bool setDeviceProperties(string deviceID, json &j);
    bool getDeviceIDForKey(string key, string &deviceID);
    
    long upTime();  // how long since we started
    bool getSolarEvents(solarTimes_t &solar);

    void setActiveConnections(bool isActive);
    bool setValues(keyValueMap_t states);

    bool initDataBase();

    pIoTServerDB*        getDB() {return &_db; };

      // sequence
    bool startRunningSequence(sequenceID_t sid,
                        boolCallback_t callback = NULL);
    

    bool abortSequence(sequenceID_t sid);
 
    bool updateValuesFromSnapShot(vector<pIoTServerDB::numericValueSnapshot_t> & snapshot);

private:
 
    static pIoTServerMgr *    _sharedInstance;
    mt19937                        _rng;

    string                  _assetDirectoryPath;
    string                  _propsFileName;

    bool                    processEvents();        // do stuff here when we are not busy.
    bool                    _shouldReconcileEvents;        // part of startup  combine any unrun events.

    bool                    _running;                //Flag for starting and terminating the main loop
    std::thread             _thread;                //Internal thread
    std::condition_variable _valuesUpdated;
 
    bool                    _hasActiveConnections;

    static constexpr uint64_t DEFAULT_PROBE_SLEEP_SECONDS = 10;
     uint64_t                 _probeSleepTime = DEFAULT_PROBE_SLEEP_SECONDS;

    void serverThread();
  
    void  runSequenceForAppEvent(EventTrigger::app_event_t, boolCallback_t cb);
    
    void  executeSequenceAppEvent(sequenceID_t sid,  boolCallback_t callback = NULL);

    bool  runSequenceStep(sequenceID_t sid, uint stepNo,
                                 boolCallback_t callback = NULL);
    
    
    bool runAbortActions(sequenceID_t sid);

    
    pIoTServerDB                       _db;
    mutable std::mutex              _deviceMutex;
  
    typedef struct {
      pIoTServerDevice*       device;
        string              deviceID;
        vector<string>      keys;
    } deviceEntry_t;
    vector<deviceEntry_t>             _devices;
    
    pIoTServerDevice* deviceForKey(string key);
 
    string createUniqueDeviceID();

    
    pIoTServerDevice* createpIoTServerDevice(string driverName, string deviceID);
    
    void registerpIoTServerDevice(string name, pIoTServerDevice::builtInDevicefactoryCallback_t factory);
   /* pIoTServerDevice factory */
   map<string, pIoTServerDevice::builtInDevicefactoryCallback_t> _deviceFactory;
  
    bool findDriverPlugins(stringvector &paths);

};


#endif /* pIoTServerMgr_hpp */
