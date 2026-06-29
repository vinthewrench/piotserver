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

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <functional>
#include <random>
#include <signal.h>
#include <thread>
#include <unordered_set>

#include "pIoTServerDB.hpp"
#include "SolarTimeMgr.hpp"
#include "pIoTServerDevice.hpp"

using namespace std;


// -----------------------------------------------------------------------------
// pIoTServerMgrDevice
//
// Pseudo-device that represents the pIoTServer process itself.
//
// This device is used to expose server/global values through the same device
// value system as hardware drivers. Examples include CPU temperature, fan state,
// solar time values, and equation/formula-derived values.
// -----------------------------------------------------------------------------

class pIoTServerMgrDevice : public pIoTServerDevice {

public:

    static const uint64_t default_queryDelay = 5;

    pIoTServerMgrDevice();
    ~pIoTServerMgrDevice();

    bool initWithSchema(deviceSchemaMap_t deviceSchema);

    bool start();
    void stop();

    bool isConnected();
    bool setEnabled(bool enable);

    /*
     * This device always has a constant device ID.
     */
    bool getDeviceID(string &devID);

    bool setValues(keyValueMap_t kv);
    bool setInitialValues(keyValueMap_t kv);

    bool getValues(keyValueMap_t &results);

    void getProperties(json &j);

    bool getBuiltInSchemas(deviceSchemaMap_t &schemas);

private:

    bool _isSetup = false;

    /*
     * Kept for consistency with device state-machine style used by other
     * drivers. INS_RESPONSE is currently unused.
     */
    typedef enum {
        INS_UNKNOWN = 0,
        INS_IDLE,
        INS_INVALID,
        INS_RESPONSE
    } in_state_t;

    in_state_t _state = INS_UNKNOWN;

    /*
     * Last time this pseudo-device refreshed its values.
     */
    timeval _lastQueryTime = {};

    /*
     * Minimum seconds between value refreshes.
     */
    uint64_t _queryDelay = default_queryDelay;

    /*
     * Cached solar event values for the server location.
     */
    solarTimes_t _cachedSolarTimes;

    /*
     * Schema metadata for global/server values exposed by this pseudo-device.
     */
    typedef struct globalVarEntry_t {
        string              title;
        bool                readOnly    = false;
        bool                isEquation  = false;
        valueSchemaUnits_t  units       = UNKNOWN;
    } globalVarEntry_t;

    std::map<std::string, globalVarEntry_t> _globalValues;

    /*
     * Formula-backed pseudo-values.
     *
     * formula    = configured expression
     * lastResult = last evaluated string result
     */
    typedef struct formulaEntry_t {
        string formula;
        string lastResult;
    } formulaEntry_t;

    std::map<std::string, formulaEntry_t> _formulas;

    /*
     * Last eTag observed for global/formula value changes.
     */
    eTag_t _lastEtag = MAX_ETAG;

    bool getCPUTemp(double &tempOut);
    bool getFanState(uint8_t &state);
};


// -----------------------------------------------------------------------------
// pIoTServerMgr
//
// Main server manager singleton.
//
// Owns:
//   - database instance
//   - loaded devices/plugins
//   - server background thread
//   - event/rule/sequence execution
//   - device polling
//   - controlled shutdown coordination
//
// Important shutdown rule:
//
//   Device drivers may request a controlled shutdown, but they must not perform
//   the shutdown sequence directly. The manager owns the sequence because it is
//   the only layer that can stop normal work, quiesce device polling, arm the
//   POWERCONTROL device, release I2C/GPIO, and then invoke Linux shutdown in
//   the correct order.
// -----------------------------------------------------------------------------

class pIoTServerMgr {

public:

    static const char* pIoTServerMgr_Version;

    static pIoTServerMgr *shared() {
        if(!_sharedInstance) {
            _sharedInstance = new pIoTServerMgr;
        }
        return _sharedInstance;
    }

    typedef string piSensorKey_t;

    pIoTServerMgr();
    ~pIoTServerMgr();

    void setAssetDirectoryPath(string filePath);
    void setPropFileName(string name);

    void start();
    void stop();

    bool isRunning() {
        return _running;
    };

    // -------------------------------------------------------------------------
    // Controlled system shutdown
    // -------------------------------------------------------------------------

    /**
     * @brief Request a controlled system shutdown.
     *
     * This is intended to be called by a driver such as SHUTDOWN_SIG after a
     * hardware shutdown condition has been confirmed.
     *
     * This function is deliberately lightweight and thread-safe. It only marks
     * shutdown requested, records the reason, wakes the server thread, and
     * returns.
     *
     * It must not:
     *
     *   - stop devices directly
     *   - touch I2C directly
     *   - send POWERCONTROL commands directly
     *   - invoke Linux shutdown directly
     *
     * The actual shutdown sequence must run from the manager/server path.
     *
     * @param reason Human-readable shutdown reason for logging.
     *
     * @return true if this call created the shutdown request.
     * @return false if shutdown was already requested.
     */
    bool requestSystemShutdown(const string& reason);

    /**
     * @brief Return true once controlled shutdown has been requested.
     *
     * Normal device polling, rules, sequences, and new actions should avoid
     * starting new work once this returns true.
     *
     * @return true if shutdown has been requested.
     */
    bool isSystemShutdownRequested() const {
        return _systemShutdownRequested.load();
    };

    // -------------------------------------------------------------------------
    // Device lifecycle / polling
    // -------------------------------------------------------------------------

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

    long upTime();
    bool getSolarEvents(solarTimes_t &solar);

    void setActiveConnections(bool isActive);
    bool setValues(keyValueMap_t states);

    bool initDataBase();

    pIoTServerDB* getDB() {
        return &_db;
    };

    // -------------------------------------------------------------------------
    // Sequences / rules
    // -------------------------------------------------------------------------

    bool startRunningSequence(sequenceID_t sid,
                              boolCallback_t callback = NULL);

    bool abortSequence(sequenceID_t sid);

    bool updateValuesFromSnapShot(vector<pIoTServerDB::numericValueSnapshot_t> &snapshot);

    bool triggerRule(ruleID_t rid);

private:

    static pIoTServerMgr *_sharedInstance;

    mt19937 _rng;

    string _assetDirectoryPath;
    string _propsFileName;

    /*
     * Background processing hooks.
     */
    bool processEvents();

    /*
     * Startup reconciliation flag.
     *
     * Used after startup sequence completion to reconcile any unrun/pending
     * events.
     */
    bool _shouldReconcileEvents = false;

    /*
     * Main server thread state.
     */
    bool                    _running = false;
    std::thread             _thread;
    std::condition_variable _valuesUpdated;

    // -------------------------------------------------------------------------
    // Controlled shutdown state
    // -------------------------------------------------------------------------

    /**
     * @brief One-shot controlled shutdown request flag.
     *
     * false = normal operation
     * true  = controlled shutdown requested or already in progress
     *
     * This is atomic because the request may originate from a driver polling
     * path while the server thread is running independently.
     */
    std::atomic<bool> _systemShutdownRequested { false };

    /**
     * @brief Protects shutdown metadata.
     *
     * The atomic flag handles the one-shot request decision. This mutex protects
     * the reason string and any future shutdown metadata.
     */
    std::mutex _systemShutdownMutex;

    /**
     * @brief Human-readable reason recorded when shutdown was requested.
     */
    string _systemShutdownReason;

    /**
     * @brief Run the controlled shutdown sequence.
     *
     * Intended final order:
     *
     *   1. Stop accepting/starting normal work.
     *   2. Let current device polling/action calls finish.
     *   3. Arm POWERCONTROL delayed shutdown while I2C is still available.
     *   4. Stop devices and release GPIO/I2C.
     *   5. Invoke Linux shutdown.
     *
     * This should be called from the manager/server-owned path, not directly
     * from a driver callback or GPIO thread.
     *
     * @return true if the sequence was started/completed as expected.
     */
    bool runControlledShutdown();

    /**
     * @brief Send delayed shutdown request to POWERCONTROL.
     *
     * This is manager-owned because POWERCONTROL is an I2C device and normal
     * device traffic must be quiet before the final command is sent.
     *
     * @return true if POWERCONTROL accepted the delayed shutdown request.
     */
    bool armPowerManagerForShutdown();

    /**
     * @brief Invoke Linux shutdown.
     *
     * Expected final command:
     *
     *   sudo /usr/sbin/shutdown now
     *
     * @return true if the shutdown command was invoked successfully.
     */
    bool invokeLinuxShutdown();

    /**
     * @brief Return whether controlled shutdown support is available.
     *
     * Controlled shutdown requires both:
     *
     *   - SHUTDOWN_SIG to detect BAT_LOW
     *   - POWERCONTROL to accept the delayed shutdown command
     *
     * If POWERCONTROL is missing, disabled, or disconnected, the shutdown feature
     * is considered unavailable. In that case BAT_LOW may still be reported as an
     * incident, but pIoTServer must not arm shutdown or halt Linux automatically.
     *
     * @return true if controlled shutdown can be safely used.
     */
    bool isControlledShutdownAvailable();

    bool shutdownRuntime();

    // -------------------------------------------------------------------------
    // Rule evaluation
    // -------------------------------------------------------------------------

    static constexpr uint64_t DEFAULT_RULE_EVAL_INTERVAL_SEC = 60;

    uint64_t _evalIntervalSec = DEFAULT_RULE_EVAL_INTERVAL_SEC;
    time_t   _nextRuleEvalTime = 0;

    /*
     * True when clients currently have active connections.
     */
    bool _hasActiveConnections = false;

    /*
     * Manager-level polling gate.
     *
     * Devices also have their own query delays. This value controls how often
     * the manager attempts a polling pass at all.
     */
    static constexpr uint64_t DEFAULT_PROBE_SLEEP_SECONDS = 10;
    uint64_t _probeSleepTime = DEFAULT_PROBE_SLEEP_SECONDS;

    void serverThread();

    void runSequenceForAppEvent(EventTrigger::app_event_t event,
                                boolCallback_t cb);

    void executeSequenceAppEvent(sequenceID_t sid,
                                 boolCallback_t callback = NULL);

    bool runSequenceStep(sequenceID_t sid,
                         uint stepNo,
                         boolCallback_t callback = NULL);

    bool runAbortActions(sequenceID_t sid);

    bool refreshRuleEvalInterval();
    bool evaluateRulesIfNeeded(time_t localNow);
    bool evaluateRules(time_t localNow);

    bool fireRule(ruleID_t rid, time_t localNow, bool manual);
    void evaluateRuleTriggerSide(ruleID_t rid, time_t localNow);
    void evaluateRuleClearSide(ruleID_t rid, time_t localNow);

    bool evaluateRuleExpression(string expression, bool &isTrue);
    bool runRuleActions(ruleID_t rid);
    bool runRuleClearActions(ruleID_t rid);

    bool runActionList(const vector<Action>& actions,
                       bool ignoreManualMode,
                       bool dontLog,
                       const string& ownerLabel,
                       EventTrigger* callbackTrigger);

    // -------------------------------------------------------------------------
    // Database / devices / plugin factory
    // -------------------------------------------------------------------------

    pIoTServerDB _db;

    /*
     * Protects _devices and device calls that must not overlap with manager
     * shutdown coordination.
     */
    mutable std::mutex _deviceMutex;

    typedef struct {
        pIoTServerDevice* device;
        string            deviceID;
        vector<string>    keys;
    } deviceEntry_t;

    vector<deviceEntry_t> _devices;

    pIoTServerDevice* deviceForKey(string key);
    pIoTServerDevice* deviceForActionKey(string key);

    string createUniqueDeviceID();

    pIoTServerDevice* createpIoTServerDevice(string driverName, string deviceID);

    void registerpIoTServerDevice(string name,
                                  pIoTServerDevice::builtInDevicefactoryCallback_t factory);

    /*
     * Built-in device factory map.
     */
    map<string, pIoTServerDevice::builtInDevicefactoryCallback_t> _deviceFactory;

    bool findDriverPlugins(stringvector &paths);
};

#endif /* pIoTServerMgr_hpp */
