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
#include <ctime>

constexpr string_view JSON_ARG_PROXY = "proxy_key";

void Sprinkler_Device::clearCmdQueue()
{
    std::queue<cmd_t> empty;
    std::swap(_cmdQueue, empty);
}

constexpr string_view Driver_Version = "1.1.0 dev 0";

bool Sprinkler_Device::getVersion(string &str)
{
    str = Driver_Version;
    return true;
}

Sprinkler_Device::Sprinkler_Device(string devID) : Sprinkler_Device(devID, string()) {};

Sprinkler_Device::Sprinkler_Device(string devID, string driverName)
{
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

Sprinkler_Device::~Sprinkler_Device()
{
    stop();
}

bool Sprinkler_Device::initWithSchema(deviceSchemaMap_t deviceSchema)
{
    _proxyMap.clear();
    _boosterRelay = {};
    _masterRelay = {};
    clearCmdQueue();

    _boosterDuration = 0;
    _runUpDuration = 2;
    _runDownDuration = 2;

    for(const auto& [key, entry] : deviceSchema) {
        if(!entry.otherProps.is_null()) {
            json j = entry.otherProps;

            if(j.count(JSON_ARG_PROXY) && j[JSON_ARG_PROXY].is_string()) {
                string proxy = j[JSON_ARG_PROXY];

                if(entry.units == BOOSTER) {
                    _boosterRelay.name = key;
                    _boosterRelay.proxyName = proxy;
                    _boosterRelay.state = false;
                    _boosterRelay.enabled = true;

                    unsigned long duration = 0;
                    if(j.contains(JSON_ARG_DURATION)
                       && JSON_value_toUnsigned(j[JSON_ARG_DURATION], duration)) {
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

bool Sprinkler_Device::start()
{
    bool status = false;

    if(_deviceID.size() == 0) {
        LOGT_DEBUG("Sprinkler_Device has no deviceID");
        return false;
    }

    if(status) {
        // wait for preflight
        _deviceState = DEVICE_STATE_DISCONNECTED;
    }
    else {
        // LOGT_ERROR("Sprinkler_Device(%02X) begin FAILED: %s", i2cAddr, strerror(errno));
        _deviceState = DEVICE_STATE_ERROR;
    }

    return status;
}

bool Sprinkler_Device::preflight()
{
    bool success = false;
    clearCmdQueue();

    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    pIoTServerDB::valueSchema_t schema;
    keyValueMap_t kv;

    if(_boosterRelay.enabled) {
        schema = db->schemaForKey(_boosterRelay.proxyName);
        if(schema.units != valueSchemaUnits_t::BOOL) {
            LOGT_DEBUG("Sprinkler_Device %s property %s was not a BOOL",
                       _boosterRelay.name.c_str(),
                       _boosterRelay.proxyName.c_str());
            return false;
        }

        kv[_boosterRelay.proxyName] = "off";
    }

    if(_masterRelay.enabled) {
        schema = db->schemaForKey(_masterRelay.proxyName);
        if(schema.units != valueSchemaUnits_t::BOOL) {
            LOGT_DEBUG("Sprinkler_Device %s property %s was not a BOOL",
                       _masterRelay.name.c_str(),
                       _masterRelay.proxyName.c_str());
            return false;
        }

        kv[_masterRelay.proxyName] = "off";
    }

    for(auto [key, valve] : _proxyMap) {
        if(valve.enabled) {
            schema = db->schemaForKey(valve.proxyName);
            if(schema.units != valueSchemaUnits_t::BOOL) {
                LOGT_DEBUG("Sprinkler_Device %s property %s was not a BOOL",
                           valve.name.c_str(),
                           valve.proxyName.c_str());
                return false;
            }

            kv[valve.proxyName] = "off";
        }
    }

    // turn them all off
    success = pIoTServer->setValues(kv);

    if(success) {
        _stateTag++;
        _deviceState = DEVICE_STATE_CONNECTED;

        _running = true;
        _thread = std::thread(&Sprinkler_Device::actionThread, this);

        LOGT_DEBUG("Sprinkler_Device(%s) ready", _deviceID.c_str());
    }

    return true;
}

void Sprinkler_Device::stop()
{
    LOGT_DEBUG("Sprinkler_Device(%s) stop", _deviceID.c_str());
    _deviceState = DEVICE_STATE_DISCONNECTED;

    doShutDown();

    // wait for action thread to complete
    while(!_thread.joinable()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    _thread.join();
}

bool Sprinkler_Device::setEnabled(bool enable)
{
    if(enable) {
        _isEnabled = true;

        if(_deviceState == DEVICE_STATE_CONNECTED) {
            return true;
        }

        // force restart
        stop();

        bool success = start();
        return success;
    }

    _isEnabled = false;

    if(_deviceState == DEVICE_STATE_CONNECTED) {
        stop();
    }

    return true;
}

bool Sprinkler_Device::isConnected()
{
    return _deviceState == DEVICE_STATE_CONNECTED;
}

bool Sprinkler_Device::getValues(keyValueMap_t &results)
{
    bool hasData = false;

    if(!isConnected()) {
        return false;
    }

    if(_stateTag != _lastReportedTag) {
        std::lock_guard<std::mutex> lock(_cmdMutex);

        if(_boosterRelay.enabled) {
            results[_boosterRelay.name] = to_string(_boosterRelay.state);
        }

        if(_masterRelay.enabled) {
            results[_masterRelay.name] = to_string(_masterRelay.state);
        }

        for(auto [key, valve] : _proxyMap) {
            if(valve.enabled) {
                results[valve.name] = to_string(valve.state);
            }
        }

        _lastReportedTag = _stateTag;
        hasData = true;
    }

    return hasData;
}

bool Sprinkler_Device::setValues(keyValueMap_t kv)
{
    if(!isConnected()) {
        return false;
    }

    vector<cmd_t> newCmds;

    // queue up commands
    for(auto [key, val] : kv) {
        if(_proxyMap.count(key)) {
            valve_t valve = _proxyMap[key];
            bool requestedState;

            if(stringToBool(val, requestedState)) {
                newCmds.push_back({key, requestedState});
            }
        }
    }

    if(newCmds.size()) {
        {
            std::lock_guard<std::mutex> lock(_cmdMutex);
            for(auto cmd : newCmds) {
                // cout << "PUSH " << cmd.first << " " << cmd.second << endl;
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

bool Sprinkler_Device::doShutDown()
{
    vector<cmd_t> newCmds;

    // queue up commands
    for(auto [key, valve] : _proxyMap) {
        if(valve.enabled) {
            newCmds.push_back({key, false});
        }
    }

    {
        std::lock_guard<std::mutex> lock(_cmdMutex);
        for(auto cmd : newCmds) {
            // cout << "PUSH " << cmd.first << " " << cmd.second << endl;
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

void Sprinkler_Device::actionThread()
{
    /*
     This is a fairly complicated state machine.

     We have to handle the option of having a booster relay, which goes on
     before any new valve opens. We use the booster so we can drive sprinkler
     valves with DC current. The idea is that we can use a higher voltage
     to open the valve and then run it at a lower voltage once opened.

     The master valve is an option that some systems use ahead of any sprinkler
     valves, either as a device to prevent keeping pressure in the pipes or to
     pump water into the system. There is a _runDownDuration specified so that
     we don't chatter the master valve between close of one valve and open of
     the next, providing hysteresis.
     */

    time_t runUpStarted = MAX_TIME;
    time_t runDownStarted = MAX_TIME;
    time_t boosterStarted = MAX_TIME;

    _state = INS_IDLE;

    while(_running) {
        bool hasMasterValve = _masterRelay.enabled;
        bool hasBooster = _boosterRelay.enabled;

        bool hasONCmd = false;

        bool isWaiting = (_state == INS_RUNUP)
                      || (_state == INS_RUNDOWN)
                      || (_state == INS_BOOST);

        if(isWaiting) {
            /* sleep for a little */
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
        else {
            std::unique_lock<std::mutex> lock(_goalMtx);
            _cv.wait(lock, [this]{ return _goalChanged; });
        }

        time_t now = time(NULL);

        // Is this command going to switch on a new device?
        {
            std::lock_guard<std::mutex> lock(_cmdMutex);

            if(!_cmdQueue.empty()) {
                for(auto it = _cmdQueue.begin(); it != _cmdQueue.end(); ++it) {
                    cmd_t cmd = *it;
                    string key = cmd.first;
                    bool newState = cmd.second;

                    if(newState) {
                        if(!_proxyMap.count(key)
                           || !_proxyMap[key].state) {
                            hasONCmd = true;
                            // cout << "CMD QUEUE: " << _cmdQueue.size() << " has ON " << _state << endl;
                        }
                    }
                }
            }
        }

        if(_state == INS_BOOST) {
            if(hasONCmd) {
                // We were boosting and need to re-boost.
                boosterStarted = now;
                _state = INS_BOOST;
            }
            else if(difftime(now, boosterStarted) > static_cast<double>(_boosterDuration)) {
                setBoosterValve(false);
                boosterStarted = MAX_TIME;
                _state = INS_RUN;
            }
        }

        if(_state == INS_IDLE) {
            if(hasONCmd) {
                if(hasMasterValve) {
                    setMasterValve(true);
                    runUpStarted = now;
                    _state = INS_RUNUP;
                }
                else if(hasBooster) {
                    setBoosterValve(true);
                    boosterStarted = now;
                    _state = INS_BOOST;
                }
                else {
                    _state = INS_RUN;
                }
            }
        }

        if(_state == INS_RUNUP) {
            if(difftime(now, runUpStarted) > static_cast<double>(_runUpDuration)) {
                if(hasBooster) {
                    setBoosterValve(true);
                    boosterStarted = now;
                    _state = INS_BOOST;
                }
                else {
                    _state = INS_RUN;
                }
            }
        }

        // We were running and need to re-boost.
        if((_state == INS_RUN)
           && hasONCmd
           && hasBooster) {
            setBoosterValve(true);
            boosterStarted = now;
            _state = INS_BOOST;
        }

        if(_state == INS_RUNDOWN) {
            // Do we abort a rundown?
            if(hasONCmd) {
                if(hasBooster) {
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
                if(difftime(now, runDownStarted) > static_cast<double>(_runDownDuration)) {
                    setMasterValve(false);
                    runDownStarted = MAX_TIME;
                    _state = INS_IDLE;
                    // cout << "IDLE" << endl;
                }
            }
        }

        if(_state == INS_BOOST
           || _state == INS_RUN) {
            std::lock_guard<std::mutex> lock(_cmdMutex);

            while(!_cmdQueue.empty()) {
                // cout << "CMD QUEUE: " << _cmdQueue.size() << endl;

                auto cmd = _cmdQueue.front();
                string key = cmd.first;
                bool newState = cmd.second;

                // Are we changing existing valve states?
                if(!_proxyMap.count(key)
                   || (_proxyMap[key].state != newState)) {
                    setProxyValve(key, newState);
                }

                _cmdQueue.pop();
            }

            // Are there any valves open?
            bool valvesOpen = false;
            for(auto valve : _proxyMap) {
                if(valve.second.state) {
                    valvesOpen = true;
                }
            }

            if(!valvesOpen) {
                // Did we shut down during a boost?
                if(_state == INS_BOOST) {
                    setBoosterValve(false);
                    boosterStarted = MAX_TIME;
                }

                if(hasMasterValve) {
                    _state = INS_RUNDOWN;
                    // cout << "Start RunDown" << endl;
                    runDownStarted = now;
                }
                else {
                    _state = INS_IDLE;
                    // cout << "IDLE" << endl;
                }
            }
        }
    }
}

void Sprinkler_Device::setBoosterValve(bool state)
{
    if(_boosterRelay.enabled) {
        // cout << "Turn " << (state ? "ON " : "OFF") << " Booster" << endl;

        auto pIoTServer = pIoTServerMgr::shared();
        pIoTServer->setValues({{_boosterRelay.proxyName, to_string(state)}});

        _boosterRelay.state = state;
        _stateTag++;
    }
}

void Sprinkler_Device::setMasterValve(bool state)
{
    if(_masterRelay.enabled) {
        // cout << "Turn " << (state ? "ON " : "OFF") << " Master" << endl;

        auto pIoTServer = pIoTServerMgr::shared();
        pIoTServer->setValues({{_masterRelay.proxyName, to_string(state)}});

        _masterRelay.state = state;
        _stateTag++;
    }
}

void Sprinkler_Device::setProxyValve(string key, bool state)
{
    if(_proxyMap.count(key) && _proxyMap[key].enabled) {
        // cout << "Turn " << (state ? "ON " : "OFF") << " " << key << endl;

        auto pIoTServer = pIoTServerMgr::shared();
        pIoTServer->setValues({{_proxyMap[key].proxyName, to_string(state)}});

        _proxyMap[key].state = state;
        _stateTag++;
    }
}
