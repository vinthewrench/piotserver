//
//  pIoTServerMgr.cpp
//  pIoTServer
//
//  Created by vinnie on 12/2/24.
//

#include <sys/ioctl.h>                                                  // Serial Port IO Controls
#include <fcntl.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include <dlfcn.h>

#include <chrono>
#include <algorithm>
#include <map>
#include <ranges>

#include "pIoTServerMgr.hpp"
#include "LogMgr.hpp"
#include "PropValKeys.hpp"
#include "Utils.hpp"
#include "ServerCmdValidators.hpp"
#include "RPi_RelayBoardDevice.hpp"
#include "W1_Device.hpp"
#include "Actuator_Device.hpp"
#include "TankDepth_Device.hpp"
#include "Sprinkler_Device.hpp"
#include "VELM6030_Device.hpp"

#include "pIoTServerEvaluator.hpp"

using namespace nlohmann;

const char*     pIoTServerMgr::pIoTServerMgr_Version = "1.1.0 dev 0";


// MARK: - pIoTServerMgrDevice

// pIoTServerMgrDevice is a psuedo device that represents the server itself
// mostly we use it to hold global variables.

pIoTServerMgrDevice::pIoTServerMgrDevice(){
    
    _isSetup = false;
    _state = INS_UNKNOWN;
    _lastQueryTime = {0,0};
    
    json j = {
        { PROP_DEVICE_MFG_URL, {"/sys/class/thermal/thermal_zone0/temp",
            "https://pip.raspberrypi.com/categories/685-whitepapers-app-notes/documents/RP-003608-WP/Cooling-a-Raspberry-Pi-device.pdf",
            "https://www.raspberrypi.com/products/active-cooler/"
        }},
        { PROP_DEVICE_MFG_PART, "Raspberry Pi Active Cooler"},
    };
    setProperties(j);
    
    _deviceState = DEVICE_STATE_UNKNOWN;
    _isEnabled = true;   // this device can not be disabled
}

pIoTServerMgrDevice::~pIoTServerMgrDevice(){
    
}

bool pIoTServerMgrDevice::initWithSchema(deviceSchemaMap_t deviceSchema){
    
    for(const auto& [key, entry] : deviceSchema) {
        globalVarEntry_t e;
        e.title     = entry.title;
        e.readOnly  = entry.readOnly;
        e.units     = entry.units;
        e.isEquation = entry.isFormula;
        _globalValues[key] = e;
    }
    
    _queryDelay =  default_queryDelay;
    _deviceState = DEVICE_STATE_DISCONNECTED;
    
    _isSetup = true;
    return _isSetup;
}


/*
 {
 "device_type": "CPU",
 "pins": [
 {
 "data_type": "TEMPERATURE",
 "key": "CPU_TEMPERATURE",
 "title": "CPU Temperature",
 "tracking":  "track.changes"
 },
 {
 "data_type": "INT",
 "key": "CPU_FAN",
 "title": "CPU Cooling Fan"
 }
 ]
 },
 */
bool pIoTServerMgrDevice::getBuiltInSchemas(  deviceSchemaMap_t &schemas ){
    deviceSchemaMap_t sch =
    {
        { VAL_CPU_TEMPERATURE, {"CPU Temperature", DEGREES_C,  TR_DONT_RECORD, true }},
        { VAL_CPU_FAN, {"CPU Cooling Fan", INT,  TR_DONT_RECORD, true }},
        
        {VAL_SOLAR_SUNSET, {"Sunset Mins from Midnight", FLOAT, TR_DONT_RECORD, true }},
        {VAL_SOLAR_SUNRISE,{"Sunrise Mins from Midnight", FLOAT, TR_DONT_RECORD, true }},
        {VAL_SOLAR_CIVIL_SUNRISE, {"Civil Sunrise  Mins from Midnight", FLOAT, TR_DONT_RECORD, true }},
        {VAL_SOLAR_CIVIL_SUNSET, {"Civil Sunset from Midnight", FLOAT, TR_DONT_RECORD,  true }},
        
        {VAL_SOLAR_LASTMIDNIGHT, {"Previous Midnight", TIME_T, TR_DONT_RECORD, true }},
        {VAL_SOLAR_MOONPHASE,   {"Phase of Moon", POM, TR_DONT_RECORD, false }},
    };
    
    schemas = sch;
    return true;
}

bool pIoTServerMgrDevice::start(){
    bool status = false;
    
    if(!_isSetup){
        LOGT_DEBUG("pIoTServerMgrDevice begin called before initWithKey");
        return  false;
    }
    
    LOGT_DEBUG("pIoTServerMgrDevice begin");
    
    
    _lastQueryTime = {0,0};
    _state = INS_IDLE;
    _deviceState = DEVICE_STATE_CONNECTED;
    
    status = true;
    return status;
}

void pIoTServerMgrDevice::stop(){
    
    LOGT_DEBUG("pIoTServerMgrDevice stop");
    
    _lastQueryTime = {0,0};
    _state = INS_UNKNOWN;
    _deviceState = DEVICE_STATE_DISCONNECTED;
    
}

// this device can not be disabled
bool pIoTServerMgrDevice::setEnabled(bool enable){
    if(enable) return true;
    else return  false;;
}


bool pIoTServerMgrDevice::isConnected(){
    
    return _isSetup ;
}

bool pIoTServerMgrDevice::getDeviceID(string  &devID){
    devID = PROP_DEVICE_BUILT_IN;
    return true;
}

bool pIoTServerMgrDevice::setInitialValues(keyValueMap_t kv){
    bool status = true;
    
    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();
    
    for(const auto& [key, valStr] : kv){
        if(_globalValues.count(key)){
            
            auto sch = _globalValues[key];
            
            if(sch.isEquation){
                formulaEntry_t entry = {  valStr };
                _formulas[key] = entry;
            }
            else {
                // dont record null initial values
                if(valStr.size())
                    db->insertValue(key, valStr);
            }
        }
        else
        {
            status = false;
        }
    }
    
    return status;
}

// this enforces read only bits

bool pIoTServerMgrDevice::setValues(keyValueMap_t kv){
    
    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();
    
    for(const auto& [key, valStr] : kv){
        if(_globalValues.count(key)) {
            if(_globalValues[key].readOnly)
                return false;
            else
                db->insertValue(key, valStr);
        }
        else
            return false;
    }
    
    return true;
}


void  pIoTServerMgrDevice::getProperties(json  &j){
    j = _deviceProperties;
    string devID; getDeviceID(devID);       // PROP_DEVICE_ID is a psuedo prop
    j[PROP_DEVICE_ID] = devID;
    
    j[JSON_ARG_ENABLE] = isEnabled();
    
    vector<string> keys;
    
    for (const auto& key : _globalValues | std::views::keys)
        keys.push_back(key);
    
    deviceSchemaMap_t built_in_schemas;
    getBuiltInSchemas(built_in_schemas);
    
    for (const auto& key : built_in_schemas | std::views::keys)
        keys.push_back(key);
    
    
    j[PROP_KEYS] = keys;
}


bool pIoTServerMgrDevice::getValues( keyValueMap_t &results){
    
    bool hasData = false;
    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();
    
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
                results[VAL_CPU_TEMPERATURE] = to_string(tempC);
                gettimeofday(&_lastQueryTime, NULL);
                
                if(getFanState(fanState)){
                    results[VAL_CPU_FAN] = to_string(fanState);
                }
                hasData = true;
            }
        }
        
        // always run these tasks
        {
            solarTimes_t solar;
            
            if(SolarTimeMgr::shared()->getSolarEventTimes(solar)){
                // Solar values have update
                if(solar.previousMidnight != _cachedSolarTimes.previousMidnight){
                    _cachedSolarTimes = solar;
                    
                    results[VAL_SOLAR_LASTMIDNIGHT] = to_string(solar.previousMidnight - solar.gmtOffset);
                    results[VAL_SOLAR_SUNRISE] = to_string(solar.sunriseMins);
                    results[VAL_SOLAR_SUNSET] = to_string(solar.sunSetMins);
                    results[VAL_SOLAR_CIVIL_SUNRISE] = to_string(solar.civilSunRiseMins);
                    results[VAL_SOLAR_CIVIL_SUNSET] = to_string(solar.civilSunSetMins);
                    results[VAL_SOLAR_MOONPHASE] = to_string(solar.moonPhase);
                    db->nextEtag();
                    hasData = true;
                }
            }
        }
        // evaluate any formulas
        if(_formulas.size()){
            vector<pIoTServerDB::numericValueSnapshot_t> vars = {};
            
            // dont do this unless the DB has update
            if(db->lastEtag() != _lastEtag){
                _lastEtag = db->lastEtag();
                
                if( db->createValueSnapshot(&vars, results)){
                    
                    for(auto &[key,e] : _formulas){
                        valueSchemaUnits_t  units = _globalValues[key].units;
                        double result = 0;
                        if(evaluateExpression(e.formula, vars, result)) {
                            results[key] = db->normalizeStringForUnit(to_string(result), units);
                            pIoTServer->updateValuesFromSnapShot(vars);
                            hasData = true;
                        };
                    }
                }
            }
        }
    }
    return hasData;
}

bool pIoTServerMgrDevice::getCPUTemp(double & tempOut) {
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


bool pIoTServerMgrDevice::getFanState(uint8_t  &state) {
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


// MARK: - pIoTServerMgr


pIoTServerMgr *pIoTServerMgr::_sharedInstance = NULL;

static void sigHandler (int signum) {
    static uint8_t killing = 0;;
    
    // avoid double kill
    if(!killing){
        killing++;
        auto piMgr = pIoTServerMgr::shared();
        piMgr->stop();
    }
    
    exit(EXIT_SUCCESS);
}


#if defined(__APPLE__)
// used for cross compile on osx
#define DLL_EXT ".dylib"
#else
#define DLL_EXT ".so"
#endif


// register PI Server Device
#define RPSD(_devName_, _driver_) \
registerpIoTServerDevice(string(_devName_),     \
[=](string devID) { return new _driver_(devID); });

void pIoTServerMgr::registerpIoTServerDevice(string name,
                                             pIoTServerDevice::builtInDevicefactoryCallback_t factory) {
    _deviceFactory[name] = factory;
}

pIoTServerDevice* pIoTServerMgr::createpIoTServerDevice(string driverName, string deviceID) {
    
    
    /*
     special case these are actually all  QWIIC_RELAY devices
     PROP_DEVICE_QWR_16566,
     PROP_DEVICE_QWR_15093,
     PROP_DEVICE_QWR_16810,
     */
    
    if(   driverName == PROP_DEVICE_QWR_16566
       ||  driverName == PROP_DEVICE_QWR_15093
       ||  driverName == PROP_DEVICE_QWR_16810)
    {
        driverName =  "QWIIC_RELAY";
    }
    
    string dllPath = "plugins/" + driverName + DLL_EXT;
    // load the symbols
    void *handle = dlopen(dllPath.c_str(), RTLD_LAZY);
    
    pIoTServerDeviceFactory_t* create_plugin = (pIoTServerDeviceFactory_t*) dlsym(handle, "factory");
    if(create_plugin){
        // create an instance of the class
        pIoTServerDevice* plugin = create_plugin(deviceID, driverName);
        if(plugin){
            LOGT_INFO("Created plugin for %s(%s)",driverName.c_str(), deviceID.c_str() );
            return plugin;
        }
    }
    // try hardcoded
    
    pIoTServerDevice::builtInDevicefactoryCallback_t factory;
    if(_deviceFactory.count(driverName)){
        factory = _deviceFactory[driverName];
        return factory(deviceID);
    }
    
    
    LOGT_ERROR("Could not find driver for device type %s", driverName.c_str());
    return NULL;
}

bool pIoTServerMgr::findDriverPlugins(stringvector &paths){
    bool success = false;
    
    try
    {
        
        // run all plugins
        std::string path = "plugins";
        for (const auto & entry : std::filesystem::directory_iterator(path))
            if (entry.path().extension() == DLL_EXT) {
                
                paths.push_back(entry.path());
            }
        
        success = true;
    }
    catch (std::filesystem::filesystem_error const& ex)
    {
        cout << "filesystem error " << ex.what() << endl;
    }
    return success;
}

pIoTServerMgr::pIoTServerMgr(){
    
    //pIoTServerMgr::pIoTServerMgr(){ : _tankSensor(){
    //
    //    signal(SIGHUP, sigHandler);
    signal(SIGQUIT, sigHandler);
    signal(SIGTERM, sigHandler);
    signal(SIGINT, sigHandler);
    
    // create RNG engine
    constexpr std::size_t SEED_LENGTH = 8;
    std::array<uint_fast32_t, SEED_LENGTH> random_data;
    std::random_device random_source;
    std::generate(random_data.begin(), random_data.end(), std::ref(random_source));
    std::seed_seq seed_seq(random_data.begin(), random_data.end());
    _rng =  std::mt19937{ seed_seq };
    
    SolarTimeMgr::shared();   // initialize the solar time  manager - for uptime
    
    // Register the devices
    //    RPSD( PROP_SENSOR_ID_TMP10X,        TMP10X_Device);
    //    RPSD( PROP_SENSOR_ID_BME280,        BME280_Device);
    //     RPSD( PROP_SENSOR_ID_MCP3427,       MCP3427_Device);
    //    RPSD( PROP_SENSOR_ID_MCP23008,      MCP23008_Device);
    //    RPSD( PROP_DEVICE_PCA9671,          PCA9671_Device);
    //    RPSD( PROP_DEVICE_TCA9534,          TCA9534_Device);
    //    RPSD( PROP_SENSOR_ID_QWIIC_BUTTON,  QwiicButton_Device);
    //    RPSD( PROP_SENSOR_ID_SHT25,         SHT25_Device);
    //    RPSD( PROP_SENSOR_ID_SHT30,         SHT30_Device);
    //    RPSD( PROP_DEVICE_QWR_16566,         QWIIC_RELAY_Device);
    //    RPSD( PROP_DEVICE_QWR_15093,         QWIIC_RELAY_Device);
    //    RPSD( PROP_DEVICE_QWR_16810,         QWIIC_RELAY_Device);
    //    RPSD( PROP_SENSOR_ID_ADS1115,        ADS1115_Device);
    
    RPSD( PROP_SENSOR_ID_1WIRE,         W1_Device);
    RPSD( PROP_SENSOR_ID_VELM6030,      VELM6030_Device);
    RPSD( PROP_DEVICE_ACTUATOR,         Actuator_Device);
    RPSD( PROP_DEVICE_GPIO,             RPi_RelayBoardDevice);
    RPSD( PROP_SENSOR_ID_TANKDEPTH,     TankDepth_Device);
    RPSD( PROP_DEVICE_SPRINKLER,        Sprinkler_Device);
    
    stringvector paths;
    findDriverPlugins(paths);
    
    // start the thread running
    _shouldReconcileEvents    = false;
    
}

void pIoTServerMgr::setAssetDirectoryPath(string path){
    _assetDirectoryPath = path;
}

void pIoTServerMgr::setPropFileName(string name){
    _propsFileName = name;
}


pIoTServerMgr::~pIoTServerMgr(){
    
    if(_running)
        stop();
    
    {
        std::lock_guard<std::mutex> lock(_deviceMutex);
        
        for( auto &e: _devices){
            e.device->stop();
            delete(e.device);
        }
        _devices.clear();
    }
    
}


bool pIoTServerMgr::initDataBase( ){
    
    bool success = false;
    
      if(!_db.restorePropertiesFromFile(_propsFileName, _assetDirectoryPath))
        throw pIoTServerException("restorePropertiesFromFile Failed");
    
    // setup logfile path
    string str;
    _db.getConfigProperty(string(PROP_CONFIG_LOGFILE_PATH), str);
    if(!str.empty()){
        LogMgr::shared()->setLogFilePath(str);
    }
    else {
        
        string  logfileName = "logfile.txt";
        
        string logfilepath  = _assetDirectoryPath.empty()
                ?logfileName
                : _assetDirectoryPath + "/" + logfileName;
   
        LogMgr::shared()->setLogFilePath(logfilepath);
    }
    
    _db.getConfigProperty(string(PROP_CONFIG_LOGFILE_FLAGS), str);
    if(!str.empty()){
        char* p;
        long val = strtol(str.c_str(), &p, 0);
        if(*p == 0){
            LogMgr::shared()->_logFlags = (uint8_t)val;
        }
    }
    
    success =  _db.initLogDatabase(_assetDirectoryPath);
    
    return success;
}

void pIoTServerMgr::start(){
    
    if(_running)
        return;
    
    try {
        initDataBase();
        loadGlobalValues();
        
        LOGT_DEBUG("Start pIoTServer");
        startDevices();
        setupDeviceNotifications();
        
        _db.logAlert(ALERT_START );
        _valuesUpdated.notify_all();
        
        // the pIoTServerMgr is the data collection thread.
        //  it can be delayed without effecting the REST thread
        
        _running = true;
        _thread = std::thread(&pIoTServerMgr::serverThread, this);
    }
    catch ( const pIoTServerException& e)  {
        
        // display error on fail..
        
        LOGT_ERROR("\tpIoTServerException %d %s", e.getErrorNumber(), e.what());
        fprintf(stderr, "\tpIoTServerException %d %s", e.getErrorNumber(), e.what());
        exit(EXIT_FAILURE);
    }
    catch ( const Exception& e)  {
        
        // display error on fail..
        fprintf(stderr, "\tException %d %s", e.getErrorNumber(), e.what());
        exit(EXIT_FAILURE);
        
    }
    catch (std::invalid_argument& e)
    {
        // display error on fail..
        fprintf(stderr, "\tinvalid_argument %s",  e.what());
        exit(EXIT_FAILURE);
        
    }
}

void pIoTServerMgr::setupDeviceNotifications(){
    
    sequenceID_t sid;
    
    Sequence seq = Sequence(
                            EventTrigger(string("{\"cron\": \"0 */1 * * * ?\"}")),
                            Action([=, this](EventTrigger trig){
                                std::lock_guard<std::mutex> lock(_deviceMutex);
                                
                                for( auto &e: _devices){
                                    e.device->eventNotification(trig);
                                }
                                return true;
                            })
                            );
    seq.setName("Device Notifications: cron");
    seq.setSouldIgnoreLog(true);
    seq.setEnable(true);
    _db.sequenceSave(seq,&sid);
}


void pIoTServerMgr::stop(){
    
    if(!_running)
        return;
    
    LOGT_DEBUG("Stop pIoTServerMgr");
    
    _db.logAlert(ALERT_SHUTDOWN );
    
    runSequenceForAppEvent(EventTrigger::APP_EVENT_SHUTDOWN, [=, this]( bool didSucceed) {
        LOGT_DEBUG("Completed Shutdown events");
        
        _shouldReconcileEvents = false;
        stopDevices();
        
        _running = false;
        // wait for server thread to complete
        while(!_thread.joinable()){
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        _thread.join();
    });
}




void pIoTServerMgr::setActiveConnections(bool isActive){
    _hasActiveConnections = isActive;
}
// MARK: - Global Values

bool pIoTServerMgr::loadGlobalValues(){
    bool success = false;
    
    keyValueMap_t       kv = {};
    deviceSchemaMap_t   deviceSchemaMap;
    json j;
    if(_db.getJSONProperty(string(JSON_ARG_VALUES), &j)){
        if(j.is_array()) {
            
            for( auto entry :j){
                if(entry.is_object()
                   && entry.contains(PROP_KEY)
                   && entry.contains(PROP_DATA_TYPE)
                   )
                {
                    string key = entry[PROP_KEY];
                    string dataType = entry[PROP_DATA_TYPE];
                    
                    string title =
                    entry.contains(PROP_TITLE)? entry[PROP_TITLE] : "";
                    
                    valueTracking_t tracking = TR_DONT_RECORD;
                    if(entry.contains(PROP_TRACKING))
                        tracking = trackingValueForString(entry[PROP_TRACKING]);
                    
                    bool isReadOnly = false;  // globals default to read write
                    if( entry.contains(PROP_READONLY)
                       && entry[PROP_READONLY].is_boolean())
                        isReadOnly = entry[PROP_READONLY];
                    
                    bool isFormula = false;
                    if(entry.contains(JSON_ARG_FORMULA))
                        isFormula = true;
                    
                    valueSchemaUnits_t  schemaUnit =  schemaUnitsForString(dataType);
                    if(schemaUnit == UNKNOWN) continue;
                    
                    deviceSchema_t schema = {
                        .title  = title,
                        .units  = schemaUnit,
                        .tracking = tracking,
                        .readOnly = isReadOnly,
                        .isFormula = isFormula,
                    };
                    
                    deviceSchemaMap[key] = schema;
                    
                    if(entry.contains(JSON_ARG_FORMULA)
                       && entry[JSON_ARG_FORMULA].is_string()){
                        string formula = entry[JSON_ARG_FORMULA];
                        kv[key] = formula;
                    }
                    else
                        kv[key] = string();
                    
                    // only add intial value if it's not already in DB
                    if(!_db.isKeyInDB(key) &&
                       entry.contains(JSON_ARG_INITIAL_VALUE)){
                        string value = JSON_value_toString(entry[JSON_ARG_INITIAL_VALUE]);
                        kv[key] = value;
                    }
                }
            }
        }
        
        if(deviceSchemaMap.size()){
            pIoTServerMgrDevice *device = new(pIoTServerMgrDevice);
            string deviceID;
            
            {
                deviceSchemaMap_t built_in_schemas;
                if(device->getBuiltInSchemas(built_in_schemas)){
                    for(auto [key,schema]: built_in_schemas){
                        _db.addSchema(key,
                                      schema.units,
                                      schema.title,
                                      schema.tracking,
                                      schema.readOnly);
                    }
                }
            }
            // create a vector of device keys as a property
            if(device->getDeviceID(deviceID) ) {
                deviceEntry_t       devEntry;
                devEntry.device     = device;
                devEntry.deviceID   = deviceID;
                
                if(device->initWithSchema(deviceSchemaMap)){
                    for(auto [key,schema]: deviceSchemaMap){
                        devEntry.keys.push_back(key);
                        _db.addSchema(key,
                                      schema.units,
                                      schema.title,
                                      schema.tracking,
                                      schema.readOnly);
                    }
                    
                    // load up the initial values
                    device->setInitialValues(kv);
                    
                    // add device to the our list fo devices
                    _devices.push_back(devEntry);
                    
                    // start device
                    device->start();
                }
            }
            else {
                LOGT_ERROR("Internal Error pIoTServerMgrDevice did not return ID\n");
            }
        }
    }
    return success;
}


// MARK: - Devices



string pIoTServerMgr::createUniqueDeviceID(){
    std::uniform_int_distribution<long> distribution(SHRT_MIN,SHRT_MAX);
    string deviceID;
    
    // get a vector of existing device IDs
    vector<string>deviceIDs;
    for(deviceEntry_t e : _devices)
        deviceIDs.push_back(e.deviceID);
    
    do{
        uint16_t devID = distribution(_rng);
        deviceID  = to_string(devID);
        if (find(deviceIDs.begin(), deviceIDs.end(), deviceID) == deviceIDs.end())
            break;
    }while(true);
    
    return deviceID;
}

void pIoTServerMgr::startDevices(){
    
    {
        std::lock_guard<std::mutex> lock(_deviceMutex);
        json j;
        
        _probeSleepTime = DEFAULT_PROBE_SLEEP_SECONDS;
        
        if(_db.getJSONProperty(string(PROP_DEVICES), &j)){
            if(j.is_array())
                for( auto entry :j){
                    if(entry.is_object()
                       && entry.contains(PROP_DEVICE_TYPE)
                       && (entry.contains(PROP_DEVICE_PINS)
                           ||  (entry.contains(PROP_KEY) && entry.contains(PROP_DATA_TYPE) ))
                       ){
                        string deviceType = entry[PROP_DEVICE_TYPE];
                        
                        // device must have a non null deviceID
                        string deviceID;
                        
                        if( entry.contains(PROP_DEVICE_ID)){
                            vector<string>deviceIDs;
                            for(deviceEntry_t e : _devices)
                                deviceIDs.push_back(e.deviceID);
                            
                            deviceID = to_string(entry[PROP_DEVICE_ID]);
                            
                            if (find(deviceIDs.begin(), deviceIDs.end(), deviceID) != deviceIDs.end()){
                                LOGT_DEBUG("Device ID %s is already being used ", deviceID.c_str());
                                break;
                            }
                        }else {
                            deviceID = createUniqueDeviceID();
                        }
                        
                        bool isReadOnly = true;
                        if( entry.contains(PROP_READONLY)
                           && entry[PROP_READONLY].is_boolean())
                            isReadOnly = entry[PROP_READONLY];
                        
                        bool isEnabled = true;
                        if( entry.contains(JSON_ARG_ENABLE)
                           && entry[JSON_ARG_ENABLE].is_boolean())
                            isEnabled = entry[JSON_ARG_ENABLE];
                        
                        string deviceAddress =
                        entry.contains(PROP_ADDRESS)? entry[PROP_ADDRESS] : "";
                        
                        string deviceTitle =
                        entry.contains(PROP_TITLE)? entry[PROP_TITLE] : "";
                        
                        //                        string deviceDescription =
                        //                        entry.contains(PROP_DESCRIPTION)? entry[PROP_DESCRIPTION] : "";
                        //
                        valueTracking_t tracking = TR_DONT_RECORD;
                        if(entry.contains(PROP_TRACKING))
                            tracking = trackingValueForString(entry[PROP_TRACKING]);
                        
                        json otherProps = NULL;
                        if(entry.contains(PROP_OTHER)) otherProps = entry[PROP_OTHER];
                        
                        transform(deviceType.begin(), deviceType.end(), deviceType.begin(), ::toupper);
                        
                        // create a device
                        pIoTServerDevice *device = createpIoTServerDevice(deviceType, deviceID);
                        if(!device){
                            LOGT_ERROR("Device %s is not a valid name",deviceType.c_str() );
                            continue;
                        };
                        
                        deviceSchemaMap_t deviceSchemaMap;
                        
                        uint64_t queryDelay = UINT64_MAX;
                        
                        if(entry.contains(PROP_SENSOR_QUERY_INTERVAL)
                           && entry[PROP_SENSOR_QUERY_INTERVAL].is_number())
                            queryDelay = entry[PROP_SENSOR_QUERY_INTERVAL];
                        
                        // use the shortest probe delay we can
                        if(queryDelay < _probeSleepTime)
                            _probeSleepTime = queryDelay;
                        
                        if(entry.contains(PROP_DEVICE_PINS)){
                            if(!entry[PROP_DEVICE_PINS].is_array()) continue;
                            
                            for(json j1 : entry[PROP_DEVICE_PINS])
                                if( j1.contains(PROP_KEY) &&
                                   j1.contains(PROP_DATA_TYPE)) {
                                    string sensorKey = j1[PROP_KEY];
                                    string dataType = j1[PROP_DATA_TYPE];
                                    uint8_t pinNo = 0;
                                    json pinProps = NULL;
                                    
                                    //  GPIO has BVM mapping
                                    if(j1.contains(PROP_DEVICE_GPIO_BCM)
                                       && j1[PROP_DEVICE_GPIO_BCM].is_number())
                                        pinNo =  j1[PROP_DEVICE_GPIO_BCM];
                                    
                                    // or PROP_DEVICE_PCA9671_BIT is pinNo
                                    if(j1.contains(PROP_DEVICE_PCA9671_BIT)
                                       && j1[PROP_DEVICE_PCA9671_BIT].is_number())
                                        pinNo =  j1[PROP_DEVICE_PCA9671_BIT];
                                    
                                    if(j1.contains(PROP_OTHER))
                                        pinProps = j1[PROP_OTHER];
                                    
                                    valueTracking_t pinTracking = tracking;
                                    
                                    if(j1.contains(PROP_TRACKING))
                                        pinTracking = trackingValueForString(j1[PROP_TRACKING]);
                                    
                                    if(j1.contains(PROP_SENSOR_QUERY_INTERVAL)
                                       && j1[PROP_SENSOR_QUERY_INTERVAL].is_number())
                                        queryDelay = j1[PROP_SENSOR_QUERY_INTERVAL];
                                    
                                    bool pinReadOnly = isReadOnly;
                                    
                                    if( j1.contains(PROP_READONLY)
                                       && j1[PROP_READONLY].is_boolean())
                                        pinReadOnly = j1[PROP_READONLY];
                                    
                                    if( j1.contains(PROP_GPIO_MODE)
                                       && j1[PROP_GPIO_MODE].is_string()){
                                        
                                        string gpiostr = j1[PROP_GPIO_MODE];
                                        transform(gpiostr.begin(), gpiostr.end(), gpiostr.begin(), ::tolower);
                                        
                                        if(gpiostr.compare(PROP_ARG_GPIO_OUTPUT)== 0){
                                            pinReadOnly = false;
                                        }
                                        else if(gpiostr.compare(PROP_ARG_GPIO_INPUT)== 0){
                                            pinReadOnly = true;
                                        }
                                    }
                                    
                                    
                                    unsigned long gpioFlags = 0;
                                    
                                    if( j1.contains(PROP_GPIO_FLAGS)){
                                        JSON_value_toUnsigned(j1[PROP_GPIO_FLAGS], gpioFlags);
                                    }
                                    
                                    // use the shortest probe delay we can
                                    if(queryDelay < _probeSleepTime)
                                        _probeSleepTime = queryDelay;
                                    
                                    string pinTitle = deviceTitle;
                                    if( j1.contains(PROP_TITLE))pinTitle = j1[PROP_TITLE];
                                    
                                    valueSchemaUnits_t  schemaUnit =  schemaUnitsForString(dataType);
                                    if(schemaUnit == UNKNOWN) continue;
                                    
                                    deviceSchema_t schema = {
                                        .title = pinTitle,
                                        .units = schemaUnit,
                                        .tracking = pinTracking,
                                        .readOnly = pinReadOnly,
                                        .queryDelay = queryDelay,
                                        .pinNo      = pinNo,         // used for Relays
                                        .flags      = gpioFlags,
                                        .otherProps = pinProps
                                    };
                                    
                                    deviceSchemaMap[sensorKey] = schema;
                                }
                            
                        }else {
                            string sensorKey = entry[PROP_KEY];
                            string dataType = entry[PROP_DATA_TYPE];
                            
                            valueSchemaUnits_t  schemaUnit =  schemaUnitsForString(dataType);
                            if(schemaUnit == UNKNOWN) continue;
                            
                            deviceSchema_t schema = {
                                .title  = deviceTitle,
                                .units  = schemaUnit,
                                .tracking = tracking,
                                .readOnly = isReadOnly,
                                .queryDelay = queryDelay,
                                .otherProps = otherProps
                            };
                            deviceSchemaMap[sensorKey] = schema;
                        }
                        
                        // set device value keys  schema
                        stringvector deviceKeys;
                        
                        if(device->initWithSchema(deviceSchemaMap)){
                            for(auto [key,schema]: deviceSchemaMap){
                                deviceKeys.push_back(key);
                                _db.addSchema(key,
                                              schema.units,
                                              schema.title,
                                              schema.tracking,
                                              schema.readOnly);
                            }
                            
                            // set device properties
                            // this is important since we at least need the device address
                            json deviceProps;
                            
                            static vector<string_view> filter_table = {
                                PROP_KEY ,
                                PROP_READONLY,
                                PROP_SENSOR_QUERY_INTERVAL,
                                PROP_TRACKING,
                                PROP_DATA_TYPE,
                                PROP_DEVICE_PINS,
                            };
                            
                            // create a vector of device keys as a property
                            for (auto& [key, value] : entry.items())
                                if ( std::find(filter_table.begin(), filter_table.end(), key) == filter_table.end() )
                                    deviceProps[key] = value;
                            
                            deviceProps[PROP_KEYS] = deviceKeys;
                            device->setProperties(deviceProps);
                            
                            string deviceID;
                            
                            // every device needs a deviceID
                            if(device->getDeviceID(deviceID) ) {
                                
                                deviceEntry_t devEntry;
                                devEntry.device = device;
                                devEntry.deviceID = deviceID;
                                
                                for (auto const& e : deviceSchemaMap)
                                    devEntry.keys.push_back(e.first);
                                
                                // add device to the our list fo devices
                                _devices.push_back(devEntry);
                                
                                if(isEnabled){
                                    // start device
                                    device->start();
                                }
                                else{
                                    device->setEnabled(false);
                                }
                            }
                            else {
                                LOGT_ERROR("Device did not return ID\n");
                            }
                            
                        }
                    }
                }
        }
        
    }
    
    // call preflight after all devices are installed
    
    for(deviceEntry_t e : _devices){
        if(e.device->isEnabled()){
            if(! e.device->preflight()){
                e.device->setEnabled(false);
                LOGT_ERROR("Device %s failed preflight\n", e.deviceID.c_str());
            }
        }
    }
}


void pIoTServerMgr::getAllDeviceStatus(json &j){
    
    json devicePropArray;
    
    {
        std::lock_guard<std::mutex> lock(_deviceMutex);
        
        for(deviceEntry_t e : _devices){
            json props;
            
            string deviceID;
            e.device->getDeviceID(deviceID);
            
            props[PROP_DEVICE_ID] = deviceID;
            props[JSON_ARG_ENABLE] =  e.device->isEnabled();
            
            pIoTServerDevice::device_state_t deviceState = e.device->getDeviceState();
            props[JSON_ARG_STATE] = deviceState;
            props[JSON_ARG_STATE_STRING] =  pIoTServerDevice::stateString(deviceState);
            devicePropArray.push_back(props);
        }
    }
    j = devicePropArray;
}


void pIoTServerMgr::getAllDeviceProperties(json &j){
    
    json deviceProps;
    
    {
        std::lock_guard<std::mutex> lock(_deviceMutex);
        
        for(deviceEntry_t e : _devices){
            json props;
            pIoTServerDevice::device_state_t deviceState = e.device->getDeviceState();
            
            e.device->getProperties(props);
            props[JSON_ARG_STATE] = deviceState;
            props[JSON_ARG_STATE_STRING] =  pIoTServerDevice::stateString(deviceState);
            
            deviceProps[e.deviceID] = props;
        }
    }
    j = deviceProps;
}

bool pIoTServerMgr::getDeviceProperties(string deviceID, json &j){
    bool status = false;
    
    for(deviceEntry_t e : _devices){
        if(deviceID == e.deviceID){
            
            pIoTServerDevice::device_state_t deviceState = e.device->getDeviceState();
            e.device->getProperties(j);
            
            j[JSON_ARG_STATE] = deviceState;
            j[JSON_ARG_STATE_STRING] =  pIoTServerDevice::stateString(deviceState);
            status = true;
            break;
        }
    }
    return status;
}

bool pIoTServerMgr::setDeviceProperties(string deviceID, json &j){
    bool didUpdate = false;
    
    json changedProps;
    
    for(deviceEntry_t e : _devices){
        if(deviceID == e.deviceID){
            
            for(auto it =  j.begin(); it != j.end(); ++it) {
                string key = Utils::trim(it.key());
                
                // special properties
                if(key == JSON_ARG_ENABLE) {
                    if(it.value().is_boolean()){
                        bool shouldEnable  = it.value();
                        didUpdate =  e.device->setEnabled(shouldEnable);
                        if(didUpdate) changedProps[key] = it.value();
                    }
                }
                
                // OTHER PROPERTIES REQUIRE UPDATING THE JSON.PROPS FILE
                // DONt try and be everthing to everybody
            }
            break;
        }
    }
    
    if(changedProps.size() ){
        json deviceProps;
        if(_db.getJSONProperty(string(JSON_ARG_DEVICES),&deviceProps)){
            for(auto &entry: deviceProps){
                
                // device must have a non null deviceID
                string devID =  to_string(entry[PROP_DEVICE_ID]);
                if(devID != deviceID)continue;
                
                for(auto it =  changedProps.begin(); it != changedProps.end(); ++it) {
                    entry[it.key()] = it.value();
                }
                _db.setProperty(string(JSON_ARG_DEVICES), deviceProps);
                _db.saveProperties();
                
                break;
            }
        }
    }
    return didUpdate;
};


pIoTServerDevice* pIoTServerMgr::deviceForKey(string key){
    pIoTServerDevice* device = NULL;
    
    {
        std::lock_guard<std::mutex> lock(_deviceMutex);
        for(deviceEntry_t e : _devices){
            auto found = std::find(e.keys.begin(), e.keys.end(), key);
            if (found != e.keys.end()){
                device = e.device;
            }
        }
    }
    return device;
}

bool  pIoTServerMgr::getDeviceIDForKey(string key, string &deviceID){
    bool success = false;
    
    for( auto d : _devices){
        if(find(d.keys.begin(),d.keys.end(), key) != d.keys.end()){
            deviceID = d.deviceID;
            success = true;
            break;
        }
    }
    return success;
}

bool pIoTServerMgr:: setValues(keyValueMap_t kv){
    bool success = false;
    
    for( auto d : _devices){
        keyValueMap_t forThisDevice;
        
        // check if we can set the key
        for(auto s : kv){
            if(find(d.keys.begin(),d.keys.end(), s.first) != d.keys.end()){
                string key = s.first;
                string value = s.second;
                if(_db.isValidDataTypeForKey(key, value)){
                    forThisDevice[key] = value;
                    //   LOGT_DEBUG("Set %s : %s", key.c_str(), value.c_str());
                    success = true;
                }
            }
        }
        
        if(forThisDevice.size()){
            if(d.device->isEnabled()){
                if(d.device->setValues(forThisDevice)){
                    // if success then update DB immediately
                    _db.insertValues(forThisDevice);
                    success = true;
                }
                else {
                    string devID;
                    string devName =  d.device->getDeviceTitle();
                    d.device->getDeviceID(devID);
                    
                    string keys;
                    for(auto&  [k,v] : forThisDevice){
                        if(!keys.empty()) keys += ", ";
                        keys += k;
                    }
                    
                    LOGT_ERROR("SetValues (%s) device: %s \"%s\" FAILED",
                               keys.c_str(),
                               devID.c_str(),
                               devName.c_str()
                               );
                }
            }
            else {
                string devID;
                d.device->getDeviceID(devID);
                LOGT_ERROR("setValues %s FAILED device not enabled\n", devID.c_str());
            }
        }
    }
    if(success)
        _valuesUpdated.notify_all();
    
    return success;
}

bool pIoTServerMgr::stopDevices(){
    bool success = false;
    
    {
        std::lock_guard<std::mutex> lock(_deviceMutex);
        
        for( auto &e: _devices){
            
            e.device->allOff();
            e.device->stop();
        }
    }
    return success;
}

//MARK: -  background event / device thread

void pIoTServerMgr::serverThread(){
    
    bool shouldRunStartup = true;
    bool waitForStartupToComplete = false;
    
    while(_running){
        
        std::mutex cv_m;
        std::unique_lock<std::mutex> lk(cv_m);
        
        if(shouldRunStartup){
            shouldRunStartup = false;
            waitForStartupToComplete = true;
            runSequenceForAppEvent(EventTrigger::APP_EVENT_STARTUP, [=,this, &waitForStartupToComplete]( bool didSucceed) {
                waitForStartupToComplete = false;
                _shouldReconcileEvents = true;
            });
        }
        
        if(waitForStartupToComplete){
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        
        // read data from devices
        readDevices();
        
        // process any timed events
        processEvents();
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // sleep the thread for BACKGROUND_THREAD_SLEEP_SECONDS or on _valuesUpdated notification
        //      _valuesUpdated.wait_for(lk, std::chrono::seconds(BACKGROUND_THREAD_SLEEP_SECONDS));
        
        
        //        if(_esyBox.needsLogin()){
        //
        //            string dab_user, dab_secret;
        //            _db.getProperty(string(PROP_DAB_USER), &dab_user);
        //            _db.getProperty(string(PROP_DAB_SECRET), &dab_secret);
        //            if(dab_user.size() && dab_secret.size())
        //                _esyBox.login(dab_user, dab_secret);
        //         }
        //
        //        if(_esyBox.isConnected()){
        //            // handle input
        //            _esyBox.rcvResponse([=]( map<string,string> results){
        //                _db.updateValues(results);
        //            });
        //        }
        //        _esyBox.idle();
        
    };
}

bool pIoTServerMgr::readDevices(){
    
    //    limit this to _probeSleepTime
    
    static time_t lastRun = 0;
    
    time_t now = time(NULL);
    int didUpdate = 0;
    
    if(now  >= (lastRun + _probeSleepTime )){
        
        std::lock_guard<std::mutex> lock(_deviceMutex);
        lastRun = now;
        
        for( auto &e: _devices){
            keyValueMap_t results;
            
            if( e.device->hasUpdates() &&
               e.device->getValues(results) ) {
                if( _db.insertValues(results)) didUpdate++;
            };
        }
    }
    return didUpdate > 0;
}


bool pIoTServerMgr::processEvents(){
    
    int ranEvents = 0;
    
    // good place to check for events.
    solarTimes_t solar;
    if(SolarTimeMgr::shared()->getSolarEventTimes(solar)){
        
        time_t now = time(NULL);
        struct tm* tm = localtime(&now);
        time_t localNow  = (now + tm->tm_gmtoff);
        
        // combine any unrun events.
        if(_shouldReconcileEvents) {
            //            _db.reconcileEventGroups(solar, localNow);
            _db.reconcileSequenceGroup(solar, localNow);
            _shouldReconcileEvents = false;
        }
        
        auto sids = _db.sequencesThatNeedToRunNow(solar, localNow);
        for (auto sid : sids) {
            
            bool should_abort =  !_db.sequenceEvaluateCondition(sid);
            bool dontLog = _db.sequenceShouldIgnoreLog(sid);
            
            { // DEBUG
                uint stepNo;
                if(_db.sequenceNextStepNumberToRun(sid,stepNo)
                   && stepNo == 0) {
                    
                    EventTrigger trig;
                    _db.sequenceGetTrigger(sid,trig);
                    string trgiStr =  trig.printString();
                    
                    uint  count;
                    _db.sequenceStepsCount(sid, count);
                    
                    string name = _db.sequenceGetName(sid);
                    string condition = _db.sequenceGetCondition(sid);
                    
                    if(!dontLog)
                        LOGT_INFO("RUN %s SEQUENCE %04x (%d steps) \"%s\"",
                                  trgiStr.c_str(),
                                  sid, count, name.c_str());
                }
            }// DEBUG
            
            if(should_abort){
                
                string name = _db.sequenceGetName(sid);
                string condition = _db.sequenceGetCondition(sid);
                
                string msg = "ABORTED SEQUENCE " + SequenceID_to_string(sid)
                +  " \"" + name  +  "\" condition: \"  " + condition +  "\"";
                
                _db.logAlert(ALERT_MESSAGE, msg);
                
                LOGT_INFO(msg.c_str());
                
                // check if we are in the middle of a sequence.
                uint stepNo = 0;
                _db.sequenceNextStepNumberToRun(sid,stepNo);
                
                // reset the sequence
                _db.sequenceReset(sid);
                _db.sequenceSetLastRunTime(sid, localNow);
                
                // if we already started a sequence,  run abort
                if(stepNo > 0){
                    runAbortActions(sid);
                }
                continue;
            }
            
            uint stepNo;
            if(_db.sequenceNextStepNumberToRun(sid,stepNo)){
                
                { // DEBUG
                    EventTrigger trig;
                    _db.sequenceGetTrigger(sid,trig);
                    string trgiStr =  trig.printString();
                    
                    bool dontLog = _db.sequenceShouldIgnoreLog(sid);
                    
                    uint  count;
                    _db.sequenceStepsCount(sid, count);
                    
                    string name = _db.sequenceGetName(sid);
                    
                    if(!dontLog)
                        LOGT_INFO("RUN %s SEQUENCE %04x, Step %d \"%s\"",
                                  trgiStr.c_str(),
                                  sid, stepNo, name.c_str());
                }// DEBUG
                
                
                runSequenceStep(sid, stepNo, [=, this]( bool didSucceed){
                    
                    time_t now = time(NULL);
                    struct tm* tm = localtime(&now);
                    time_t localNow  = (now + tm->tm_gmtoff);
                    
                    if(didSucceed){
                        if(_db.sequenceCompletedStep(sid, stepNo, localNow)){
                            //                            printf("Sequence: %04x, Step:%d  - completed -\n", sid, stepNo);
                        }
                        else {
                            // we completed..
                            _db.sequenceSetLastRunTime(sid, localNow);
                            //                           printf("Sequence: %04x, - done -\n\n", sid);
                        }
                    }
                });
            }
            
        };
    }
    return ranEvents > 0;;
}


//MARK: -  events / sequences / actions

void pIoTServerMgr::runSequenceForAppEvent(EventTrigger::app_event_t trig, boolCallback_t cb){
    
    auto seqs = _db.sequencesMatchingAppEvent(trig);
    if(seqs.empty()) {
        cb(true);
        return;
    }
    size_t* taskCount  = (size_t*) malloc(sizeof(size_t));
    *taskCount = seqs.size();
    
    for(auto sid: seqs){
        
        { // DEBUG
            EventTrigger trig;
            _db.sequenceGetTrigger(sid,trig);
            string trgiStr =  trig.printString();
            
            uint  count;
            _db.sequenceStepsCount(sid, count);
            
            string name = _db.sequenceGetName(sid);
            
            LOGT_INFO("RUN %s SEQUENCE %04x (%d steps) \"%s\"",
                      trgiStr.c_str(),
                      sid, count, name.c_str());
        }// DEBUG
        
        executeSequenceAppEvent(sid,  [=, this]( bool didSucceed){
            if(!didSucceed){
                EventTrigger trig;
                _db.sequenceGetTrigger(sid,trig);
                string trgiStr =  trig.printString();
                
                string name = _db.sequenceGetName(sid);
                
                LOGT_INFO("Failed executeSequenceAppEvent SEQUENCE %04x %s \"%s\"",
                          sid, trgiStr.c_str(), name.c_str());
                
            }
            if(--(*taskCount) == 0) {
                free(taskCount);
                // we completed startup
                
                EventTrigger trig;
                _db.sequenceGetTrigger(sid,trig);
                string trgiStr =  trig.printString();
                string name = _db.sequenceGetName(sid);
                
                LOGT_INFO("COMPLETED %s SEQUENCE %04x \"%s\"",
                          trgiStr.c_str(),
                          sid, name.c_str());
                cb(true);
            }
        });
        
    }
}

void pIoTServerMgr::executeSequenceAppEvent(sequenceID_t sid, boolCallback_t cb){
    
    uint stepNo;
    
    if(_db.sequenceNextStepNumberToRun(sid,stepNo)){
        runSequenceStep(sid, stepNo, [=, this]( bool didSucceed){
            if(didSucceed){
                
                time_t now = time(NULL);
                struct tm* tm = localtime(&now);
                time_t localNow  = (now + tm->tm_gmtoff);
                
                if(_db.sequenceCompletedStep(sid, stepNo, localNow)){
                    
                    uint64_t waitFor = _db.sequenceStepDuration(sid,stepNo);
                    if(waitFor){
                        std::this_thread::sleep_for(std::chrono::seconds(waitFor));
                    }
                    executeSequenceAppEvent(sid, cb);
                }
                else
                {
                    // we completed the last step
                    _db.sequenceReset(sid);
                    // app events dont track LastRunTime
                    //                   _db.sequenceSetLastRunTime(sid, time(NULL));
                    (cb)(true);
                }
            }
            else
            {
                _db.sequenceReset(sid);
                //               _db.sequenceSetLastRunTime(sid, time(NULL));
                (cb)(false);
            }
        });
    }
    else
    {
        // bad sequence
        (cb)(false);
    }
}


bool pIoTServerMgr::runAbortActions(sequenceID_t sid){
    bool success = true;
    
    vector<Action> actions;
    
    if(!_db.sequenceGetAbortActions(sid, actions)){
        return true;
    }
    
    for(auto action :actions){
        LOGT_DEBUG("RUN ABORT ACTION: %s", action.printString().c_str());
        
        if(action.cmd() == Action::JSON_CMD_SET){
            string key = action.key();
            string value = action.value();
            
            if(_db.isKeyInManualMode(key)){
                LOGT_ERROR("Sequence (%s) abort action ould not set key \"%s\" to %s. Manual mode only",
                           SequenceID_to_string(sid).c_str(),
                           key.c_str(), value.c_str());
                continue;
            }
            keyValueMap_t   kv;
            kv[key] = value;
            if(!setValues(kv))  {
                LOGT_ERROR("FAILED TO SET %s = %s @runAbortActions line %d",
                           key.c_str(), value.c_str(), __LINE__);
            }
        }
        else if(action.cmd() == Action::JSON_CMD_RUN_SEQ){
            string str = action.key();
            sequenceID_t sid;
            
            if( str_to_SequenceID(str.c_str(), &sid)
               && _db.sequenceIDIsValid(sid)){
                success = startRunningSequence(sid);
            }
            else
                success = false;
        }
        else if(action.cmd() == Action::JSON_CMD_EVAL){
            string expression = action.expression();
            
            vector<pIoTServerDB::numericValueSnapshot_t> vars;
            
            if( _db.createValueSnapshot(&vars)){
                double result = 0;
                if(evaluateExpression(expression, vars, result)) {
                    success = updateValuesFromSnapShot(vars);
                }
            }
        }
        else if(action.cmd() == Action::JSON_CMD_LOG){
            string detail = action.value();
            success =  _db.logAlert(ALERT_MESSAGE, detail);
        }
        else if((action.cmd() == Action::JSON_CMD_CALLBACK)
                && action.isCallBack()){
            
            EventTrigger trig;
            _db.sequenceGetTrigger(sid,trig);
            success =  action.invokeCallBack(trig);
        }
    }
    
    return success;
};

bool pIoTServerMgr::runSequenceStep(sequenceID_t sid, uint stepNo,
                                    boolCallback_t cb){
    
    bool success = true;
    bool dontLog = _db.sequenceShouldIgnoreLog(sid);
    Step step;
    
    if(!_db.sequenceGetStep(sid, stepNo, step))
        return false;
    
    vector<Action> actions;
    step.getActions(actions);
    
    for(auto action :actions){
        if(!dontLog)
            LOGT_DEBUG("RUN ACTION: %s", action.printString().c_str());
        
        if(action.cmd() == Action::JSON_CMD_SET){
            string key = action.key();
            string value = action.value();
            
            if(_db.isKeyInManualMode(key)){
                LOGT_ERROR("Sequence: (%s, %d) Could not set key \"%s\" to %s. Manual mode only",
                           SequenceID_to_string(sid).c_str(), stepNo,
                           key.c_str(), value.c_str());
                continue;
            }
            keyValueMap_t   kv;
            kv[key] = value;
            if(!setValues(kv))  {
                LOGT_ERROR("FAILED TO SET %s = %s @runSequenceStep line %d",
                           key.c_str(), value.c_str(), __LINE__);
            }
        }
        
        else if(action.cmd() == Action::JSON_CMD_RUN_SEQ){
            string str = action.key();
            sequenceID_t sid;
            
            if( str_to_SequenceID(str.c_str(), &sid) && _db.sequenceIDIsValid(sid)){
                success = startRunningSequence(sid);
            }
            else
                success = false;
        }
        else if(action.cmd() == Action::JSON_CMD_EVAL){
            string expression = action.expression();
            
            vector<pIoTServerDB::numericValueSnapshot_t> vars;
            
            if( _db.createValueSnapshot(&vars)){
                double result = 0;
                if(evaluateExpression(expression, vars, result)) {
                    success = updateValuesFromSnapShot(vars);
                }
            }
        }
        else if(action.cmd() == Action::JSON_CMD_LOG){
            string detail = action.value();
            success =  _db.logAlert(ALERT_MESSAGE, detail);
        }
        else if((action.cmd() == Action::JSON_CMD_CALLBACK)
                && action.isCallBack()){
            
            EventTrigger trig;
            _db.sequenceGetTrigger(sid,trig);
            success =  action.invokeCallBack(trig);
        }
    }
    
    if(cb) (cb)(success);
    return true;
}



// sequence
bool pIoTServerMgr::startRunningSequence(sequenceID_t sid,
                                         boolCallback_t cb){
    bool success = _db.triggerSequence(sid);
    if(cb) (cb)(success);
    return success;
}

bool pIoTServerMgr::abortSequence(sequenceID_t sid){
    
    bool success  = false;
    
    if( _db.sequenceIDIsValid(sid)){
        
        solarTimes_t solar;
        if(SolarTimeMgr::shared()->getSolarEventTimes(solar)){
            
            time_t now = time(NULL);
            struct tm* tm = localtime(&now);
            time_t localNow  = (now + tm->tm_gmtoff);
            
            string name = _db.sequenceGetName(sid);
            string condition = _db.sequenceGetCondition(sid);
            
            string msg = "ABORTED SEQUENCE " + SequenceID_to_string(sid)
            +  " \"" + name  +  "\"";
            
            _db.logAlert(ALERT_MESSAGE, msg);
            
            LOGT_INFO(msg.c_str());
            
            // check if we are in the middle of a sequence.
            uint stepNo = 0;
            _db.sequenceNextStepNumberToRun(sid,stepNo);
            
            // reset the sequence
            _db.sequenceReset(sid);
            _db.sequenceSetLastRunTime(sid, localNow);
            
            // if we already started a sequence,  run abort
            if(stepNo > 0){
                runAbortActions(sid);
            }
        }
      }
 
    return success;
}



// MARK: -   EsyBox Pump

/*
 void pIoTServerMgr::startEsyBox( std::function<void(bool didSucceed, std::string error_text)> cb){
 
 int  errnum = 0;
 bool didSucceed = false;
 
 const string ESYBOX_PREFIX = "ESYBOX";
 //    _db.addSchema(TANK_DEPTH_KEY,
 //                      PumpHouseDB::PERCENT,
 //                      "Tank Level",
 //                      PumpHouseDB::TR_TRACK_CHANGES);
 //
 //    _db.addSchema(TANK_DEPTH_RAW_KEY,
 //                      PumpHouseDB::INT,
 //                      "Tank Level Raw",
 //                      PumpHouseDB::TR_TRACK_CHANGES);
 
 if(_esyBox.begin(ESYBOX_PREFIX, errnum)){
 LOGT_INFO("Start EsyBox  - OK");
 }
 else
 LOGT_INFO("Start EsyBox  - FAIL %s", string(strerror(errnum)).c_str());
 
 
 if(cb)
 (cb)(didSucceed, didSucceed?"": string(strerror(errnum) ));
 
 
 }
 
 void pIoTServerMgr::stopEsyBox(){
 _esyBox.stop();
 }
 
 pIoTServerDevice::device_state_t pIoTServerMgr::EsyBoxState(){
 return _esyBox.getDeviceState();
 }
 
 */

// MARK: -   utilities


long pIoTServerMgr::upTime(){
    return SolarTimeMgr::shared()->upTime();
}

bool pIoTServerMgr::getSolarEvents(solarTimes_t &solar){
    return SolarTimeMgr::shared()->getSolarEventTimes(solar);
}

bool pIoTServerMgr::updateValuesFromSnapShot(vector<pIoTServerDB::numericValueSnapshot_t> & snapshot){
    bool didUpdate = false;
    
    keyValueMap_t   kv;
    
    for(auto &e: snapshot)
        if(e.wasUpdated)  {
            
            valueSchemaUnits_t unit = _db.unitsForKey(e.name);
            string norm = pIoTServerDB::normalizeStringForUnit(to_string(e.value),unit);
            kv[e.name] = norm;
        }
    
    if(kv.size()){
        if(setValues(kv))
            didUpdate = true;
    }
    
    return didUpdate;
}
