//
//  VALVEMASTER_Device.cpp
//  pIoTServer
//
//  Runtime VALVEMASTER pIoTServer plugin.
//
//  This plugin adapts the VALVEMASTER I2C/RS-485 irrigation controller to the
//  pIoTServer device interface.
//
//  Important hardware truths:
//
//    - Field valves are latching valves.
//    - Field-bus power does not hold a valve open or closed.
//    - Removing field-bus power does not close a valve.
//    - Valve nodes report commanded/logical state only.
//    - Valve nodes do not read true mechanical valve position.
//
//  Runtime design:
//
//    - initWithSchema() records diagnostic keys and valve bindings.
//    - start() opens local I2C, detects inherited field power, and optionally
//      syncs node-reported commanded state only if field power is already on.
//    - start() must not power on the field bus.
//    - getValues() is cache-drain only.
//    - setValues() queues valve set actions and returns promptly.
//    - actionThreadMain() serializes all hardware-changing work.
//    - power-on valve reset establishes a commanded-safe baseline after field
//      power was off or state trust was invalidated.
//    - power_hold_sec is a post-close field-power hold timer.
//    - retry_count / retry_delay_ms protect command-level transient failures.
//    - IncidentMgr records durable faults/recoveries for meaningful runtime
//      failures such as valve command failure, valve read failure, field power
//      failure, and reset all_off failure.
//    - queued valve set actions are coalesced by key.
//    - all_off removes queued future valve set actions because it is a
//      superseding safety action.
//    - stop() cancels idle timer, stops queued work, allOffs if water risk
//      exists, powers off if field power is known on, and releases local I2C.
//
//  Runtime plugin boundary:
//
//    - no config
//    - no assign
//    - no node address changes
//    - no EEPROM/provisioning commands
//
//  Provisioning remains CLI-only.
//

#include "VALVEMASTER_Device.hpp"

#include "LogMgr.hpp"
#include "PropValKeys.hpp"
#include "IncidentMgr.hpp"

#include <errno.h>
#include <string.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using std::string;
using std::string_view;

constexpr string_view Driver_Version = "2.0.0 dev 1";

static constexpr const char* KEY_VALVEMASTER_STATUS  = "VALVEMASTER_STATUS";
static constexpr const char* KEY_VALVEMASTER_POWER   = "VALVEMASTER_POWER";
static constexpr const char* KEY_VALVEMASTER_RESULT  = "VALVEMASTER_RESULT";
static constexpr const char* KEY_VALVEMASTER_VERSION = "VALVEMASTER_VERSION";

static constexpr uint64_t DEFAULT_QUERY_DELAY_SEC = 5;
static constexpr uint32_t FIELD_BUS_NODE_STABILIZE_MS = 1000;
static constexpr uint32_t VALVE_READ_TIMEOUT_MS = 10000;
static constexpr const char* DELAY_NODE_STABILIZE = "node-stabilize";

/*
 * VALVEMASTER controller status bit for field-bus power.
 *
 * This mirrors the wrapper/controller STATUS_POWER_ON bit. It is used only as
 * a fallback if the direct power-state register read fails.
 */
static constexpr uint8_t VALVEMASTER_STATUS_POWER_ON = 0x04;


/**
 * @brief Return an integer property from schema otherProps.
 *
 * Supports a primary key and fallback key so schema can use:
 *
 *   node/channel
 *   node/valve
 *   valve_node/valve_channel
 *
 * @param props JSON otherProps object.
 * @param primary Primary property name.
 * @param fallback Fallback property name.
 * @param value Receives integer value.
 * @return true if property was found and converted.
 */
static bool readOtherPropInt(const json& props,
                             const char* primary,
                             const char* fallback,
                             int& value)
{
    if(primary == nullptr || !props.is_object()) {
        return false;
    }

    try {
        if(props.contains(primary)) {
            if(props[primary].is_number_integer()) {
                value = props[primary].get<int>();
                return true;
            }

            if(props[primary].is_string()) {
                value = std::stoi(props[primary].get<string>());
                return true;
            }
        }

        if(fallback != nullptr && props.contains(fallback)) {
            if(props[fallback].is_number_integer()) {
                value = props[fallback].get<int>();
                return true;
            }

            if(props[fallback].is_string()) {
                value = std::stoi(props[fallback].get<string>());
                return true;
            }
        }
    }
    catch(...) {
        return false;
    }

    return false;
}



/**
 * @brief Parse a pIoTServer boolean-ish valve value.
 *
 * Accepted open/on values:
 *
 *   1, true, on, open
 *
 * Accepted closed/off values:
 *
 *   0, false, off, closed, close
 *
 * @param text Input value string.
 * @param value Receives parsed boolean.
 * @return true if parsed.
 */

static string normalizedActionCommand(const string& text)
{
    string normalized = text;

    normalized.erase(
        std::remove_if(normalized.begin(),
                       normalized.end(),
                       [](unsigned char c) {
                           return std::isspace(c);
                       }),
        normalized.end());

    std::transform(normalized.begin(),
                   normalized.end(),
                   normalized.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });

    return normalized;
}

static bool parseValveBoolValue(const string& text, bool& value)
{
    string normalized = text;

    normalized.erase(
        std::remove_if(normalized.begin(),
                       normalized.end(),
                       [](unsigned char c) {
                           return std::isspace(c);
                       }),
        normalized.end());

    std::transform(normalized.begin(),
                   normalized.end(),
                   normalized.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });

    if(normalized == "1" ||
       normalized == "true" ||
       normalized == "on" ||
       normalized == "open") {
        value = true;
        return true;
    }

    if(normalized == "0" ||
       normalized == "false" ||
       normalized == "off" ||
       normalized == "closed" ||
       normalized == "close") {
        value = false;
        return true;
    }

    return false;
}


static string quoteIncidentDetailValue(const string& value)
{
    string quoted = "\"";

    for(char c : value) {
        if(c == '"' || c == '\\') {
            quoted += '\\';
        }

        quoted += c;
    }

    quoted += "\"";

    return quoted;
}


/**
 * @brief Convert a JSON property to unsigned integer with bounds.
 *
 * Accepts integer and string JSON values. Values below minValue are clamped to
 * minValue. Values above maxValue are clamped to maxValue.
 *
 * @param object JSON object.
 * @param key Property key.
 * @param defaultValue Value used when key is missing or invalid.
 * @param minValue Minimum accepted/clamped value.
 * @param maxValue Maximum accepted/clamped value.
 * @return Parsed/clamped value.
 */
static uint32_t jsonUInt32ClampedOr(const json& object,
                                    const string& key,
                                    uint32_t defaultValue,
                                    uint32_t minValue,
                                    uint32_t maxValue)
{
    if(!object.is_object() || !object.contains(key)) {
        return defaultValue;
    }

    try {
        int64_t value = static_cast<int64_t>(defaultValue);

        if(object[key].is_number_integer()) {
            value = object[key].get<int64_t>();
        } else if(object[key].is_number_unsigned()) {
            uint64_t unsignedValue = object[key].get<uint64_t>();

            if(unsignedValue > static_cast<uint64_t>(INT64_MAX)) {
                value = INT64_MAX;
            } else {
                value = static_cast<int64_t>(unsignedValue);
            }
        } else if(object[key].is_string()) {
            value = std::stoll(object[key].get<string>());
        } else {
            return defaultValue;
        }

        if(value < static_cast<int64_t>(minValue)) {
            return minValue;
        }

        if(value > static_cast<int64_t>(maxValue)) {
            return maxValue;
        }

        return static_cast<uint32_t>(value);
    }
    catch(...) {
        return defaultValue;
    }
}


/**
 * @brief Return the VALVEMASTER pIoTServer plugin version.
 *
 * This is the plugin/driver version. It is not necessarily the firmware
 * version reported by the VALVEMASTER controller.
 *
 * @param str Receives version string.
 * @return true.
 */
bool VALVEMASTER_Device::getVersion(string &str)
{
    str = string(Driver_Version);
    return true;
}


/**
 * @brief Convenience constructor.
 *
 * @param devID pIoTServer device ID.
 */
VALVEMASTER_Device::VALVEMASTER_Device(string devID)
    : VALVEMASTER_Device(devID, string())
{
}


/**
 * @brief Construct VALVEMASTER plugin instance.
 *
 * Constructor sets static properties only. It does not touch I2C hardware and
 * does not power the RS-485 field bus.
 *
 * @param devID pIoTServer device ID.
 * @param driverName pIoTServer driver/plugin name.
 */
VALVEMASTER_Device::VALVEMASTER_Device(string devID, string driverName)
{
    setDeviceID(devID, driverName);

    json j = {
        { PROP_ADDRESS, "0x09" },
        { PROP_DEVICE_MFG_PART, "VALVEMASTER I2C to RS-485 irrigation valve controller" },
        { PROP_DESCRIPTION, "I2C VALVEMASTER plugin, cached reporting and serialized action-thread interface." },
        { PROP_OTHER, {
            { "i2c_default_address", "0x09" },
            { "start_policy", "open local I2C, detect inherited field power, sync valves only if field power is already on" },
            { "getvalues_policy", "cache drain only" },
            { "deviceaction_policy", "bounded runtime commands only; no provisioning" },
            { "setvalues_policy", "configured valve writes use SET_CHANNEL acknowledgement as success; status reads are reserved for sync/diagnostic/audit paths" },
            { "incident_policy", "durable incidents are raised only for meaningful runtime faults; retry recovery is logged as notice" },
            { "runtime_start_power_on", "disabled" },         { "runtime_start_who", "disabled" },
            { "runtime_start_valve_sync", "enabled only if inherited field power is already on" },
            { "runtime_power_on_reset", "enabled before trusted valve commands when commanded-state trust is invalid" },
            { "runtime_power_hold_sec", "post-command field-power hold timer; not a valve hold timer" },
            { "runtime_retries", "command-level retry_count and retry_delay_ms" },
            { "runtime_queue_coalescing", "queued set_valve actions coalesced by key; all_off removes queued valve actions" },
            { "runtime_stop_power_off", "synchronous power-off if field power is on; allOff first when water risk exists" },
            { "runtime_stop_all_off", "synchronous safety cleanup when water risk exists" },
            { "provisioning_config", "disabled" },
            { "provisioning_assign", "disabled" },
            { "field_bus_actions", "power_on, power_off, all_off, set_valve" },
            { "serialized_delay", "enabled for node stabilization and later timing gaps" },
            { "water_risk_state", "tracked separately from field power state" },
            { "valve_feedback_model", "node-reported commanded state only; no physical valve position feedback" }
        }}
    };

    setProperties(j);

    _deviceState = DEVICE_STATE_UNKNOWN;
    _isSetup = false;
    _isStarted = false;
    _startup_time = 0;
    _queryDelay = DEFAULT_QUERY_DELAY_SEC;
    _i2cAddress = 0x09;

    _powerHoldSec = 60;
    _retryCount = 3;
    _retryDelayMs = 150;
    _idlePowerOffArmed = false;

    _fieldPowerKnown = false;
    _fieldPowerOn = false;

    _startupFieldPowerWasOn = false;

    _valveStateKnown = false;
    _anyValveKnownOpen = false;
    _anyValveMayBeOpen = false;

    _needPowerOnValveReset = true;
}


/**
 * @brief Destroy plugin instance.
 *
 * stop() releases the local worker/I2C resources. It also performs synchronous
 * irrigation safety cleanup if the driver believes water risk exists or if
 * field power is known on.
 */
VALVEMASTER_Device::~VALVEMASTER_Device()
{
    stop();
}


/**
 * @brief Initialize plugin from pIoTServer schema.
 *
 * This method discovers the schema keys owned by this driver and records the
 * shortest queryDelay. It must not touch hardware.
 *
 * Current expected diagnostic keys:
 *
 *   - VALVEMASTER_STATUS
 *   - VALVEMASTER_POWER
 *   - VALVEMASTER_RESULT
 *   - VALVEMASTER_VERSION
 *
 * Valve keys are discovered from schema otherProps using any of these shapes:
 *
 *   { "node": N, "channel": C }
 *   { "node": N, "valve": C }
 *   { "valve_node": N, "valve_channel": C }
 *
 * The real pIoTServer farm JSON currently uses:
 *
 *   { "node": N, "valve": C }
 *
 * where valve is the node-local valve/channel number.
 *
 * @param deviceSchema pIoTServer device schema entries.
 * @return true if at least one supported schema key was found.
 */
bool VALVEMASTER_Device::initWithSchema(deviceSchemaMap_t deviceSchema)
{
    uint64_t delay = UINT64_MAX;

    _valveBindings.clear();

    for(const auto& [key, entry] : deviceSchema) {

        if(key == KEY_VALVEMASTER_STATUS) {
            _statusKey = key;
            if(entry.queryDelay < delay) delay = entry.queryDelay;
            _isSetup = true;
        }
        else if(key == KEY_VALVEMASTER_POWER) {
            _powerKey = key;
            if(entry.queryDelay < delay) delay = entry.queryDelay;
            _isSetup = true;
        }
        else if(key == KEY_VALVEMASTER_RESULT) {
            _resultKey = key;
            if(entry.queryDelay < delay) delay = entry.queryDelay;
            _isSetup = true;
        }
        else if(key == KEY_VALVEMASTER_VERSION) {
            /*
             * This key is reserved for the VALVEMASTER controller firmware
             * version, not the pIoTServer plugin version.
             *
             * start() is quiet and does not read controller firmware. A later
             * background/action diagnostic pass can populate this key.
             */
            _versionKey = key;
            if(entry.queryDelay < delay) delay = entry.queryDelay;
            _isSetup = true;
        }
        else {
            int node = 0;
            int channel = 0;
            string title = entry.title;

            bool hasNode =
                readOtherPropInt(entry.otherProps,
                                 "node",
                                 "valve_node",
                                 node);

            bool hasChannel =
                readOtherPropInt(entry.otherProps,
                                 "channel",
                                 "valve_channel",
                                 channel);

            /*
             * Real farm JSON uses:
             *
             *   "other.props": { "node": N, "valve": C }
             *
             * In this driver, "valve" means node-local output channel.
             */
            if(!hasChannel) {
                hasChannel =
                    readOtherPropInt(entry.otherProps,
                                     "valve",
                                     nullptr,
                                     channel);
            }

            if(hasNode && hasChannel) {
                if(node >= 1 && node <= 254 && channel >= 1 && channel <= 16) {
                    ValveBinding binding;
                    binding.node = static_cast<uint8_t>(node);
                    binding.channel = static_cast<uint8_t>(channel);
                    binding.title = title;

                    _valveBindings[key] = binding;

                    if(entry.queryDelay < delay) delay = entry.queryDelay;
                    _isSetup = true;
                } else {
                    LOGT_ERROR("VALVEMASTER_Device ignored valve key \"%s\": invalid node=%d channel=%d",
                               key.c_str(),
                               node,
                               channel);
                }
            }
        }
    }

    _queryDelay = delay != UINT64_MAX ? delay : DEFAULT_QUERY_DELAY_SEC;
    _deviceState = DEVICE_STATE_DISCONNECTED;

    LOGT_DEBUG("VALVEMASTER_Device devID \"%s\" setup=%s statusKey=\"%s\" powerKey=\"%s\" resultKey=\"%s\" versionKey=\"%s\" valveBindings=%zu queryDelay=%llu",
               _deviceID.c_str(),
               _isSetup ? "true" : "false",
               _statusKey.c_str(),
               _powerKey.c_str(),
               _resultKey.c_str(),
               _versionKey.c_str(),
               _valveBindings.size(),
               static_cast<unsigned long long>(_queryDelay));

    return _isSetup;
}


/**
 * @brief Update cached driver value.
 *
 * If markPending is true, the value is copied into _pendingValues so
 * pIoTServer can report it through hasUpdates()/getValues().
 *
 * This intentionally reports pending values even when the cached value did not
 * change.
 *
 * Reason:
 *
 *   setValues() first records requested state with markPending=false because
 *   pIoTServer manager immediately inserts requested values after setValues()
 *   returns true.
 *
 *   Later, the action thread reports command-completed state. For normal
 *   runtime SET_CHANNEL, success means the controller accepted the command and
 *   received an acceptable node acknowledgement. This is commanded/logical
 *   state, not physical valve-position truth.
 *  *
 * @param key pIoTServer value key.
 * @param value String value.
 * @param markPending Whether the value should be reported.
 */
void VALVEMASTER_Device::setCachedValue(const string& key,
                                        const string& value,
                                        bool markPending)
{
    if(key.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(_cacheMutex);

    _cachedValues[key] = value;

    if(markPending) {
        _pendingValues[key] = value;
    }
}

/**
 * @brief Seed initial cached values after start().
 *
 * This publishes local/plugin-known state only. It must not power the field bus,
 * run WHO, scan nodes, read valve nodes, or perform recovery.
 *
 * Important latching-valve distinction:
 *
 *   If field power is off at startup, the driver cannot know true mechanical
 *   valve position. Removing field power does not close a latching valve.
 *
 *   However, pIoTServer and REST clients still need initial BOOL values for
 *   configured output keys. When field power is known off, this function seeds
 *   configured valve output cache values as "0" for UI/REST baseline only.
 *
 *   That baseline does not establish trusted valve state. The next trusted
 *   valve command must still power the field bus, wait for node stabilization,
 *   run allOff, and only then apply the requested command.
 */
void VALVEMASTER_Device::seedStartupCache()
{
    if(!_statusKey.empty()) {
        setCachedValue(_statusKey, "connected", true);
    }

    if(!_powerKey.empty()) {
        if(_fieldPowerKnown) {
            setCachedValue(_powerKey, _fieldPowerOn ? "on" : "off", true);
        } else {
            setCachedValue(_powerKey, "unknown", true);
        }
    }

    if(!_resultKey.empty()) {
        bool resultAlreadyCached = false;

        {
            std::lock_guard<std::mutex> lock(_cacheMutex);
            resultAlreadyCached =
                (_cachedValues.find(_resultKey) != _cachedValues.end());
        }

        if(!resultAlreadyCached) {
            if(_startupFieldPowerWasOn) {
                setCachedValue(_resultKey, "startup-field-power-on", true);
            } else {
                setCachedValue(_resultKey, "none", true);
            }
        }
    }

    if(_fieldPowerKnown && !_fieldPowerOn) {
        for(const auto& [key, binding] : _valveBindings) {
            (void)binding;
            setCachedValue(key, "0", true);
        }
    }
}


/**
 * @brief Parse runtime driver properties from _deviceProperties.
 */
void VALVEMASTER_Device::parseRuntimeProperties()
{
    static constexpr uint32_t DEFAULT_POWER_HOLD_SEC = 60;
    static constexpr uint32_t MAX_POWER_HOLD_SEC = 3600;

    static constexpr uint32_t DEFAULT_RETRY_COUNT = 3;
    static constexpr uint32_t MIN_RETRY_COUNT = 1;
    static constexpr uint32_t MAX_RETRY_COUNT = 10;

    static constexpr uint32_t DEFAULT_RETRY_DELAY_MS = 150;
    static constexpr uint32_t MAX_RETRY_DELAY_MS = 5000;

    _powerHoldSec =
        jsonUInt32ClampedOr(_deviceProperties,
                            "power_hold_sec",
                            DEFAULT_POWER_HOLD_SEC,
                            0,
                            MAX_POWER_HOLD_SEC);

    _retryCount =
        jsonUInt32ClampedOr(_deviceProperties,
                            "retry_count",
                            DEFAULT_RETRY_COUNT,
                            MIN_RETRY_COUNT,
                            MAX_RETRY_COUNT);

    _retryDelayMs =
        jsonUInt32ClampedOr(_deviceProperties,
                            "retry_delay_ms",
                            DEFAULT_RETRY_DELAY_MS,
                            0,
                            MAX_RETRY_DELAY_MS);

    LOGT_DEBUG("VALVEMASTER_Device runtime properties: power_hold_sec=%u retry_count=%u retry_delay_ms=%u",
               static_cast<unsigned int>(_powerHoldSec),
               static_cast<unsigned int>(_retryCount),
               static_cast<unsigned int>(_retryDelayMs));
}


/**
 * @brief Detect inherited field-power state at plugin startup.
 */
void VALVEMASTER_Device::detectStartupFieldPowerState()
{
    uint8_t powerState = 0;

    if(_valveMaster.readFirmwarePowerState(powerState)) {
        _fieldPowerKnown = true;
        _fieldPowerOn = (powerState != 0);

        if(_fieldPowerOn) {
            _startupFieldPowerWasOn = true;

            _valveStateKnown = false;
            _anyValveKnownOpen = false;
            _anyValveMayBeOpen = true;
            _needPowerOnValveReset = true;
        } else {
            _startupFieldPowerWasOn = false;

            _valveStateKnown = false;
            _anyValveKnownOpen = false;
            _anyValveMayBeOpen = false;
            _needPowerOnValveReset = true;
        }

        return;
    }

    uint8_t status = 0;

    if(_valveMaster.readFirmwareStatus(status)) {
        _fieldPowerKnown = true;
        _fieldPowerOn = ((status & VALVEMASTER_STATUS_POWER_ON) != 0);

        if(_fieldPowerOn) {
            _startupFieldPowerWasOn = true;

            _valveStateKnown = false;
            _anyValveKnownOpen = false;
            _anyValveMayBeOpen = true;
            _needPowerOnValveReset = true;
        } else {
            _startupFieldPowerWasOn = false;

            _valveStateKnown = false;
            _anyValveKnownOpen = false;
            _anyValveMayBeOpen = false;
            _needPowerOnValveReset = true;
        }

        return;
    }

    _fieldPowerKnown = false;
    _fieldPowerOn = false;

    _startupFieldPowerWasOn = false;

    _valveStateKnown = false;
    _anyValveKnownOpen = false;
    _anyValveMayBeOpen = false;
    _needPowerOnValveReset = true;

    LOGT_ERROR("VALVEMASTER_Device startup could not read field-power state; reporting power unknown and requiring power-on valve reset before next trusted command");
}


/**
 * @brief Submit a VALVEMASTER queued command and wait for completion.
 */
bool VALVEMASTER_Device::runQueuedCommandAndWait(
        const string& actionName,
        std::function<bool(VALVEMASTERCompletion)> submit,
        uint32_t timeoutMs)
{
    std::mutex doneMutex;
    std::condition_variable doneCv;

    bool completed = false;
    bool commandOK = false;

    valvemaster_op_status_t completionStatus = {};
    unsigned char completionResult = 0;
    unsigned char completionDetail = 0;

    bool accepted = submit(
        [&](valvemaster_op_status_t status,
            unsigned char result,
            unsigned char detail) {

            {
                std::lock_guard<std::mutex> lock(doneMutex);

                completionStatus = status;
                completionResult = result;
                completionDetail = detail;

                commandOK = _valveMaster.commandSucceeded();
                completed = true;
            }

            doneCv.notify_one();
        });

    if(!accepted) {
        LOGT_ERROR("VALVEMASTER_Device action \"%s\" was not accepted by wrapper",
                   actionName.c_str());
        return false;
    }

    std::unique_lock<std::mutex> lock(doneMutex);

    bool signaled = doneCv.wait_for(
        lock,
        std::chrono::milliseconds(timeoutMs),
        [&]() {
            return completed;
        });

    if(!signaled) {
        LOGT_ERROR("VALVEMASTER_Device action \"%s\" timed out waiting for completion",
                   actionName.c_str());
        return false;
    }

    if(!commandOK) {
        LOGT_ERROR("VALVEMASTER_Device action \"%s\" completed with failure status=%d result=0x%02X detail=0x%02X",
                   actionName.c_str(),
                   static_cast<int>(completionStatus),
                   static_cast<unsigned int>(completionResult),
                   static_cast<unsigned int>(completionDetail));
        return false;
    }

    return true;
}


/**
 * @brief Run one queued command with retry policy.
 */
bool VALVEMASTER_Device::runQueuedCommandWithRetries(
        const string& commandName,
        std::function<bool(VALVEMASTERCompletion)> submit,
        uint32_t timeoutMs,
        const string& incidentDetails)
{
    const uint32_t attempts = std::max<uint32_t>(1, _retryCount);

    auto incidentValueForField = [](const string& details, const string& fieldName) -> string {
        const string prefix = fieldName + "=";

        size_t pos = details.find(prefix);
        if(pos == string::npos) {
            return "";
        }

        pos += prefix.length();

        while(pos < details.length() &&
              std::isspace(static_cast<unsigned char>(details[pos]))) {
            pos++;
        }

        if(pos >= details.length()) {
            return "";
        }

        string value;

        if(details[pos] == '"') {
            pos++;

            while(pos < details.length()) {
                char ch = details[pos++];

                if(ch == '\\' && pos < details.length()) {
                    value.push_back(details[pos++]);
                    continue;
                }

                if(ch == '"') {
                    break;
                }

                value.push_back(ch);
            }
        } else {
            size_t end = pos;

            while(end < details.length() &&
                  !std::isspace(static_cast<unsigned char>(details[end]))) {
                end++;
            }

            value = details.substr(pos, end - pos);
        }

        return value;
    };

    auto recoveredIncidentKey = [&]() -> string {
        string valveKey = incidentValueForField(incidentDetails, "key");

        if(!valveKey.empty()) {
            return valveKey;
        }

        return commandName;
    };

    auto recoveredIncidentDetails = [&](uint32_t attempt) -> string {
        string details =
            "tried=" + std::to_string(attempt) +
            "/" + std::to_string(attempts);

        string node = incidentValueForField(incidentDetails, "node");
        string channel = incidentValueForField(incidentDetails, "channel");

        if(!node.empty() && !channel.empty()) {
            details += " node=";
            details += node;
            details += "/";
            details += channel;
        }

        return details;
    };

    for(uint32_t attempt = 1; attempt <= attempts; attempt++) {
        if(attempts > 1) {
            LOGT_DEBUG("VALVEMASTER_Device command \"%s\" attempt %u/%u",
                       commandName.c_str(),
                       static_cast<unsigned int>(attempt),
                       static_cast<unsigned int>(attempts));
        }

        bool ok = runQueuedCommandAndWait(commandName,
                                          submit,
                                          timeoutMs);

        if(ok) {
            if(attempt > 1) {
                const string incidentKey = recoveredIncidentKey();
                const string details = recoveredIncidentDetails(attempt);

                IncidentMgr::shared()->notice(
                    _deviceID,
                    "COMMAND_RETRY_RECOVERED",
                    incidentKey,
                    nullptr,
                    details.c_str()
                );

                LOGT_DEBUG("VALVEMASTER_Device command \"%s\" recovered on attempt %u/%u key=\"%s\" details=\"%s\"",
                           commandName.c_str(),
                           static_cast<unsigned int>(attempt),
                           static_cast<unsigned int>(attempts),
                           incidentKey.c_str(),
                           details.c_str());
            }

            return true;
        }

        if(attempt < attempts) {
            LOGT_ERROR("VALVEMASTER_Device command \"%s\" failed attempt %u/%u; retrying",
                       commandName.c_str(),
                       static_cast<unsigned int>(attempt),
                       static_cast<unsigned int>(attempts));

            if(_retryDelayMs > 0) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(_retryDelayMs));
            }
        } else {
            LOGT_ERROR("VALVEMASTER_Device command \"%s\" failed attempt %u/%u; giving up",
                       commandName.c_str(),
                       static_cast<unsigned int>(attempt),
                       static_cast<unsigned int>(attempts));
        }
    }

    return false;
}

/**
 * @brief Read one valve with retry policy.
 */
bool VALVEMASTER_Device::readValveWithRetries(const string& reason,
                                              const string& key,
                                              uint8_t node,
                                              uint8_t channel,
                                              bool& reportedOpen)
{
    const uint32_t attempts = std::max<uint32_t>(1, _retryCount);

    string title;

    auto bindingIt = _valveBindings.find(key);
    if(bindingIt != _valveBindings.end()) {
        title = bindingIt->second.title;
    }

    reportedOpen = false;

    for(uint32_t attempt = 1; attempt <= attempts; attempt++) {
        std::mutex doneMutex;
        std::condition_variable doneCv;

        bool completed = false;
        bool commandOK = false;
        bool valveOpen = false;

        valvemaster_op_status_t completionStatus = {};
        unsigned char completionResult = 0;
        unsigned char completionDetail = 0;
        unsigned char completionNode = 0;
        unsigned char completionChannel = 0;

        bool accepted = _valveMaster.getValve(
            node,
            channel,
            [&](valvemaster_op_status_t status,
                unsigned char result,
                unsigned char detail,
                unsigned char replyNode,
                unsigned char replyChannel,
                bool isOpen) {

                {
                    std::lock_guard<std::mutex> lock(doneMutex);

                    completionStatus = status;
                    completionResult = result;
                    completionDetail = detail;
                    completionNode = replyNode;
                    completionChannel = replyChannel;

                    commandOK =
                        _valveMaster.commandSucceeded() &&
                        replyNode == node &&
                        replyChannel == channel;

                    valveOpen = isOpen;
                    completed = true;
                }

                doneCv.notify_one();
            });

        if(!accepted) {
            LOGT_ERROR("VALVEMASTER_Device %s read valve key=\"%s\" node=%u channel=%u not accepted attempt %u/%u",
                       reason.c_str(),
                       key.c_str(),
                       static_cast<unsigned int>(node),
                       static_cast<unsigned int>(channel),
                       static_cast<unsigned int>(attempt),
                       static_cast<unsigned int>(attempts));
        } else {
            std::unique_lock<std::mutex> lock(doneMutex);

            bool signaled = doneCv.wait_for(
                lock,
                std::chrono::milliseconds(VALVE_READ_TIMEOUT_MS),
                [&]() {
                    return completed;
                });

            if(!signaled) {
                LOGT_ERROR("VALVEMASTER_Device %s read valve key=\"%s\" node=%u channel=%u timed out attempt %u/%u",
                           reason.c_str(),
                           key.c_str(),
                           static_cast<unsigned int>(node),
                           static_cast<unsigned int>(channel),
                           static_cast<unsigned int>(attempt),
                           static_cast<unsigned int>(attempts));
            } else if(!commandOK) {
                LOGT_ERROR("VALVEMASTER_Device %s read valve key=\"%s\" node=%u channel=%u failed attempt %u/%u status=%d result=0x%02X detail=0x%02X replyNode=%u replyChannel=%u",
                           reason.c_str(),
                           key.c_str(),
                           static_cast<unsigned int>(node),
                           static_cast<unsigned int>(channel),
                           static_cast<unsigned int>(attempt),
                           static_cast<unsigned int>(attempts),
                           static_cast<int>(completionStatus),
                           static_cast<unsigned int>(completionResult),
                           static_cast<unsigned int>(completionDetail),
                           static_cast<unsigned int>(completionNode),
                           static_cast<unsigned int>(completionChannel));
            } else {
                reportedOpen = valveOpen;

                if(attempt > 1) {
                    string details =
                        "reason=" + reason +
                        " key=" + key +
                        (title.empty()
                            ? ""
                            : " title=" + quoteIncidentDetailValue(title)) +
                        " node=" + std::to_string(static_cast<unsigned int>(node)) +
                        " channel=" + std::to_string(static_cast<unsigned int>(channel)) +
                        " attempts=" + std::to_string(attempt) +
                        " max_attempts=" + std::to_string(attempts);

                    IncidentMgr::shared()->notice(
                        _deviceID,
                        "VALVE_READ_RETRY_RECOVERED",
                        key,
                        nullptr,
                        details.c_str()
                    );

                    LOGT_DEBUG("VALVEMASTER_Device %s read valve key=\"%s\" succeeded on attempt %u/%u",
                               reason.c_str(),
                               key.c_str(),
                               static_cast<unsigned int>(attempt),
                               static_cast<unsigned int>(attempts));
                }

                IncidentMgr::shared()->clear(
                    _deviceID,
                    "VALVE_READ_FAILED",
                    key
                );

                return true;
            }
        }

        if(attempt < attempts && _retryDelayMs > 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(_retryDelayMs));
        }
    }

    {
        string details =
            "reason=" + reason +
            " key=" + key +
            (title.empty()
                ? ""
                : " title=" + quoteIncidentDetailValue(title)) +
            " node=" + std::to_string(static_cast<unsigned int>(node)) +
            " channel=" + std::to_string(static_cast<unsigned int>(channel)) +
            " attempts=" + std::to_string(attempts);

        IncidentMgr::shared()->raise(
            _deviceID,
            IncidentMgr::Severity::Error,
            "VALVE_READ_FAILED",
            key,
            nullptr,
            details.c_str()
        );
    }

    return false;
}

/**
 * @brief Read all configured valve states from hardware and sync cache.
 */
bool VALVEMASTER_Device::syncConfiguredValveStatesFromHardware(const string& reason)
{
    if(_valveBindings.empty()) {
        _valveStateKnown = true;
        _anyValveKnownOpen = false;
        _anyValveMayBeOpen = false;
        _needPowerOnValveReset = false;

        return true;
    }

    if(!_fieldPowerKnown || !_fieldPowerOn) {
        LOGT_ERROR("VALVEMASTER_Device valve sync \"%s\" rejected: field power is not known on",
                   reason.c_str());

        _valveStateKnown = false;
        _anyValveMayBeOpen = true;
        _needPowerOnValveReset = true;

        return false;
    }

    bool allReadsOK = true;
    bool anyOpen = false;

    for(const auto& [key, binding] : _valveBindings) {
        bool valveOpen = false;

        if(!readValveWithRetries(reason,
                                 key,
                                 binding.node,
                                 binding.channel,
                                 valveOpen)) {
            allReadsOK = false;
            continue;
        }

        setCachedValue(key, valveOpen ? "1" : "0", true);

        if(valveOpen) {
            anyOpen = true;
        }
    }

    if(allReadsOK) {
        _valveStateKnown = true;
        _anyValveKnownOpen = anyOpen;
        _anyValveMayBeOpen = anyOpen;
        _needPowerOnValveReset = false;

        if(!_resultKey.empty()) {
            setCachedValue(_resultKey, "startup-valve-sync-ok", true);
        }

        return true;
    }

    _valveStateKnown = false;
    _anyValveMayBeOpen = true;
    _needPowerOnValveReset = true;

    if(!_resultKey.empty()) {
        setCachedValue(_resultKey, "startup-valve-sync-failed", true);
    }

    return false;
}


/**
 * @brief Ensure field power is on and perform power-on valve reset if needed.
 */
bool VALVEMASTER_Device::ensurePoweredAndResetIfNeeded(const string& reason)
{
    cancelIdlePowerOff();

    bool poweredOnThisCall = false;

    if(!_fieldPowerKnown || !_fieldPowerOn) {
        bool powerOK = runQueuedCommandWithRetries(
            reason + "_power_on",
            [this](VALVEMASTERCompletion completion) {
                return _valveMaster.powerOn(completion);
            },
            10000);

        if(!powerOK) {
            _fieldPowerKnown = false;
            _fieldPowerOn = false;

            _valveStateKnown = false;
            _anyValveMayBeOpen = true;
            _needPowerOnValveReset = true;

            {
                string details =
                    "reason=" + reason +
                    " command=power_on" +
                    " attempts=" + std::to_string(std::max<uint32_t>(1, _retryCount));

                IncidentMgr::shared()->raise(
                    _deviceID,
                    IncidentMgr::Severity::Error,
                    "FIELD_POWER_ON_FAILED",
                    "VALVEMASTER_POWER",
                    nullptr,
                    details.c_str()
                );
            }

            if(!_resultKey.empty()) {
                setCachedValue(_resultKey, "power-reset-power-on-failed", true);
            }

            return false;
        }

        IncidentMgr::shared()->clear(
            _deviceID,
            "FIELD_POWER_ON_FAILED",
            "VALVEMASTER_POWER"
        );

        _fieldPowerKnown = true;
        _fieldPowerOn = true;
        poweredOnThisCall = true;

        if(!_powerKey.empty()) {
            setCachedValue(_powerKey, "on", true);
        }
    }

    if(poweredOnThisCall) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(FIELD_BUS_NODE_STABILIZE_MS));
    }

    if(!_needPowerOnValveReset) {
        return true;
    }

    bool allOffOK = runQueuedCommandWithRetries(
        reason + "_reset_all_off",
        [this](VALVEMASTERCompletion completion) {
            return _valveMaster.allOff(completion);
        },
        10000);

    if(!allOffOK) {
        _valveStateKnown = false;
        _anyValveMayBeOpen = true;
        _needPowerOnValveReset = true;

        {
            string details =
                "reason=" + reason +
                " command=all_off" +
                " attempts=" + std::to_string(std::max<uint32_t>(1, _retryCount));

            IncidentMgr::shared()->raise(
                _deviceID,
                IncidentMgr::Severity::Error,
                "FIELD_RESET_ALL_OFF_FAILED",
                "VALVEMASTER_POWER",
                nullptr,
                details.c_str()
            );
        }

        if(!_resultKey.empty()) {
            setCachedValue(_resultKey, "power-reset-all-off-failed", true);
        }

        return false;
    }

    IncidentMgr::shared()->clear(
        _deviceID,
        "FIELD_RESET_ALL_OFF_FAILED",
        "VALVEMASTER_POWER"
    );

    _valveStateKnown = true;
    _anyValveKnownOpen = false;
    _anyValveMayBeOpen = false;
    _needPowerOnValveReset = false;

    for(const auto& [key, binding] : _valveBindings) {
        (void)binding;
        setCachedValue(key, "0", true);
    }

    if(!_resultKey.empty()) {
        setCachedValue(_resultKey, "power-reset-all-off-ok", true);
    }

    return true;
}

/**
 * @brief Arm the post-command field-power hold timer.
 */
void VALVEMASTER_Device::armIdlePowerOff()
{
    {
        std::lock_guard<std::mutex> lock(_actionMutex);

        if(!_fieldPowerKnown || !_fieldPowerOn) {
            _idlePowerOffArmed = false;
            return;
        }

        _idlePowerOffAt =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(_powerHoldSec);

        _idlePowerOffArmed = true;
    }

    _actionCv.notify_all();
}

/**
 * @brief Cancel the post-close idle power-off timer.
 */
void VALVEMASTER_Device::cancelIdlePowerOff()
{
    {
        std::lock_guard<std::mutex> lock(_actionMutex);
        _idlePowerOffArmed = false;
    }

    _actionCv.notify_all();
}


/**
 * @brief Return true if idle power-off timer is armed and expired.
 */
bool VALVEMASTER_Device::idlePowerOffDueLocked() const
{
    return
        _idlePowerOffArmed &&
        std::chrono::steady_clock::now() >= _idlePowerOffAt;
}


/**
 * @brief Run post-command field-power-off if the hold timer expires.
 */
bool VALVEMASTER_Device::runIdlePowerOff()
{
    {
        std::lock_guard<std::mutex> lock(_actionMutex);

        if(_stopRequested) {
            _idlePowerOffArmed = false;
            return true;
        }

        if(!_actionQueue.empty()) {
            _idlePowerOffArmed = false;
            return true;
        }

        _idlePowerOffArmed = false;
    }

    if(!_fieldPowerKnown || !_fieldPowerOn) {
        return true;
    }

    bool powerOffOK = runQueuedCommandWithRetries(
        "idle_power_off",
        [this](VALVEMASTERCompletion completion) {
            return _valveMaster.powerOff(completion);
        },
        10000);

    if(powerOffOK) {
        _fieldPowerKnown = true;
        _fieldPowerOn = false;

        if(!_powerKey.empty()) {
            setCachedValue(_powerKey, "off", true);
        }

        if(!_resultKey.empty()) {
            setCachedValue(_resultKey, "field-power-hold-off-ok", true);
        }

        return true;
    }

    _fieldPowerKnown = false;
    _needPowerOnValveReset = true;

    if(!_resultKey.empty()) {
        setCachedValue(_resultKey, "field-power-hold-off-failed", true);
    }

    return false;
}

/**
 * @brief Execute one valve set action.
 */
bool VALVEMASTER_Device::runSetValveAction(const Action& action)
{
    cancelIdlePowerOff();

    if(action.key.empty() || action.node == 0 || action.channel == 0) {
        LOGT_ERROR("VALVEMASTER_Device set valve action invalid key=\"%s\" node=%u channel=%u",
                   action.key.c_str(),
                   static_cast<unsigned int>(action.node),
                   static_cast<unsigned int>(action.channel));

        if(!_resultKey.empty()) {
            setCachedValue(_resultKey, "set-valve-invalid", true);
        }

        return false;
    }

    string title;

    auto bindingIt = _valveBindings.find(action.key);
    if(bindingIt != _valveBindings.end()) {
        title = bindingIt->second.title;
    }

    string valveDetails =
        "key=" + action.key +
        (title.empty()
            ? ""
            : " title=" + quoteIncidentDetailValue(title)) +
        " node=" + std::to_string(static_cast<unsigned int>(action.node)) +
        " channel=" + std::to_string(static_cast<unsigned int>(action.channel)) +
        " desired=" + string(action.desiredOpen ? "open" : "closed");

    if(!ensurePoweredAndResetIfNeeded("set_valve")) {
        LOGT_ERROR("VALVEMASTER_Device set valve key=\"%s\" failed power/reset prerequisite",
                   action.key.c_str());

        {
            string details = valveDetails + " stage=power_reset";

            IncidentMgr::shared()->raise(
                _deviceID,
                IncidentMgr::Severity::Error,
                "VALVE_SET_FAILED",
                action.key,
                nullptr,
                details.c_str()
            );
        }

        if(!_resultKey.empty()) {
            setCachedValue(_resultKey, "set-valve-power-reset-failed", true);
        }

        return false;
    }

    bool setOK = runQueuedCommandWithRetries(
        "set_valve",
        [this, &action](VALVEMASTERCompletion completion) {
            return _valveMaster.setValve(action.node,
                                         action.channel,
                                         action.desiredOpen,
                                         completion);
        },
        10000,
        valveDetails);

    if(!setOK) {
        LOGT_ERROR("VALVEMASTER_Device set valve failed key=\"%s\" node=%u channel=%u desired=%s",
                   action.key.c_str(),
                   static_cast<unsigned int>(action.node),
                   static_cast<unsigned int>(action.channel),
                   action.desiredOpen ? "open" : "closed");

        _valveStateKnown = false;
        _anyValveMayBeOpen = true;
        _needPowerOnValveReset = true;

        {
            string details =
                valveDetails +
                " attempts=" + std::to_string(std::max<uint32_t>(1, _retryCount));

            IncidentMgr::shared()->raise(
                _deviceID,
                IncidentMgr::Severity::Error,
                "VALVE_SET_FAILED",
                action.key,
                nullptr,
                details.c_str()
            );
        }

        if(!_resultKey.empty()) {
            setCachedValue(_resultKey, "set-valve-failed", true);
        }

        return false;
    }

    IncidentMgr::shared()->clear(
        _deviceID,
        "VALVE_SET_FAILED",
        action.key
    );

    setCachedValue(action.key, action.desiredOpen ? "1" : "0", true);

    bool anyKnownOpen = false;
    bool allConfiguredKnown = true;

    {
        std::lock_guard<std::mutex> cacheLock(_cacheMutex);

        for(const auto& [key, binding] : _valveBindings) {
            (void)binding;

            auto it = _cachedValues.find(key);

            if(it == _cachedValues.end()) {
                allConfiguredKnown = false;
                continue;
            }

            if(it->second == "1") {
                anyKnownOpen = true;
            }
        }
    }

    _valveStateKnown = allConfiguredKnown;
    _anyValveKnownOpen = anyKnownOpen;
    _anyValveMayBeOpen = anyKnownOpen || !allConfiguredKnown;

    if(allConfiguredKnown) {
        _needPowerOnValveReset = false;
    }

    if(_fieldPowerKnown && _fieldPowerOn) {
        armIdlePowerOff();
    }

    if(!_resultKey.empty()) {
        setCachedValue(_resultKey, "set-valve-ok", true);
    }

    return true;
}

/**
 * @brief Start the local action thread.
 */
void VALVEMASTER_Device::startActionThread()
{
    std::lock_guard<std::mutex> lock(_actionMutex);

    if(_actionThreadRunning) {
        return;
    }

    _stopRequested = false;
    _actionQueue.clear();
    _idlePowerOffArmed = false;

    _actionThread = std::thread(&VALVEMASTER_Device::actionThreadMain, this);
    _actionThreadRunning = true;
}


/**
 * @brief Stop the local action thread.
 */
void VALVEMASTER_Device::stopActionThread()
{
    {
        std::lock_guard<std::mutex> lock(_actionMutex);

        if(!_actionThreadRunning && !_actionThread.joinable()) {
            _stopRequested = true;
            _actionQueue.clear();
            _idlePowerOffArmed = false;
            return;
        }

        _stopRequested = true;
        _actionQueue.clear();
        _idlePowerOffArmed = false;
    }

    _actionCv.notify_all();

    if(_actionThread.joinable()) {
        _actionThread.join();
    }

    {
        std::lock_guard<std::mutex> lock(_actionMutex);
        _actionThreadRunning = false;
        _actionQueue.clear();
        _idlePowerOffArmed = false;
    }
}


/**
 * @brief Remove queued valve set actions for one key.
 */
size_t VALVEMASTER_Device::removeQueuedValveActionsForKeyLocked(const string& key)
{
    size_t removed = 0;

    auto it = _actionQueue.begin();

    while(it != _actionQueue.end()) {
        if(it->type == ACTION_SET_VALVE && it->key == key) {
            it = _actionQueue.erase(it);
            removed++;
        } else {
            ++it;
        }
    }

    return removed;
}


/**
 * @brief Remove all queued valve set actions.
 */
size_t VALVEMASTER_Device::removeAllQueuedValveActionsLocked()
{
    size_t removed = 0;

    auto it = _actionQueue.begin();

    while(it != _actionQueue.end()) {
        if(it->type == ACTION_SET_VALVE) {
            it = _actionQueue.erase(it);
            removed++;
        } else {
            ++it;
        }
    }

    return removed;
}


/**
 * @brief Queue an action for the local worker.
 */
bool VALVEMASTER_Device::enqueueAction(const Action& action)
{
    {
        std::lock_guard<std::mutex> lock(_actionMutex);

        if(_stopRequested || !_actionThreadRunning) {
            LOGT_ERROR("VALVEMASTER_Device action \"%s\" rejected: action thread not running",
                       action.name.c_str());
            return false;
        }

        _idlePowerOffArmed = false;

        if(action.type == ACTION_SET_VALVE) {
            size_t removed =
                removeQueuedValveActionsForKeyLocked(action.key);

            if(removed > 0) {
            }
        } else if(action.type == ACTION_ALL_OFF) {
            size_t removed = removeAllQueuedValveActionsLocked();

            if(removed > 0) {
            }
        }

        _actionQueue.push_back(action);
    }

    _actionCv.notify_one();
    return true;
}


/**
 * @brief Queue a serialized delay action.
 */
bool VALVEMASTER_Device::enqueueDelay(const string& name, uint32_t delayMs)
{
    Action action;
    action.type = ACTION_DELAY;
    action.name = name;
    action.delayMs = delayMs;

    return enqueueAction(action);
}


/**
 * @brief Action worker main loop.
 */
void VALVEMASTER_Device::actionThreadMain()
{
    for(;;) {
        Action action;
        bool haveAction = false;
        bool runIdleTimer = false;

        {
            std::unique_lock<std::mutex> lock(_actionMutex);

            for(;;) {
                if(_stopRequested) {
                    break;
                }

                if(!_actionQueue.empty()) {
                    action = _actionQueue.front();
                    _actionQueue.pop_front();
                    _idlePowerOffArmed = false;
                    haveAction = true;
                    break;
                }

                if(idlePowerOffDueLocked()) {
                    runIdleTimer = true;
                    break;
                }

                if(_idlePowerOffArmed) {
                    _actionCv.wait_until(lock, _idlePowerOffAt);
                } else {
                    _actionCv.wait(lock);
                }
            }

            if(_stopRequested) {
                break;
            }
        }

        if(runIdleTimer) {
            (void)runIdlePowerOff();
            continue;
        }

        if(!haveAction) {
            continue;
        }

        switch(action.type) {

            case ACTION_NOOP:
                if(!_resultKey.empty()) {
                    setCachedValue(_resultKey, "noop-ok", true);
                }

                break;

            case ACTION_DELAY:
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(action.delayMs));

                if(action.name == DELAY_NODE_STABILIZE) {
                    if(!_resultKey.empty()) {
                        setCachedValue(_resultKey, "power-on-ok", true);
                    }
                }

                break;

            case ACTION_POWER_ON: {
                bool ok = runQueuedCommandWithRetries(
                    "power_on",
                    [this](VALVEMASTERCompletion completion) {
                        return _valveMaster.powerOn(completion);
                    },
                    10000);

                if(ok) {
                    _fieldPowerKnown = true;
                    _fieldPowerOn = true;

                    if(!_powerKey.empty()) {
                        setCachedValue(_powerKey, "on", true);
                    }
                } else {
                    _fieldPowerKnown = false;
                    _needPowerOnValveReset = true;

                    if(!_resultKey.empty()) {
                        setCachedValue(_resultKey, "power-on-failed", true);
                    }
                }

                break;
            }

            case ACTION_POWER_OFF: {
                if(_anyValveMayBeOpen) {
                    LOGT_ERROR("VALVEMASTER_Device action power_off rejected: valve may be open; use all_off");

                    if(!_resultKey.empty()) {
                        setCachedValue(_resultKey, "power-off-rejected-water-risk", true);
                    }

                    break;
                }

                bool ok = runQueuedCommandWithRetries(
                    "power_off",
                    [this](VALVEMASTERCompletion completion) {
                        return _valveMaster.powerOff(completion);
                    },
                    10000);

                if(ok) {
                    _fieldPowerKnown = true;
                    _fieldPowerOn = false;
                    _needPowerOnValveReset = true;

                    if(!_powerKey.empty()) {
                        setCachedValue(_powerKey, "off", true);
                    }

                    if(!_resultKey.empty()) {
                        setCachedValue(_resultKey, "power-off-ok", true);
                    }
                } else {
                    _fieldPowerKnown = false;
                    _needPowerOnValveReset = true;

                    if(!_resultKey.empty()) {
                        setCachedValue(_resultKey, "power-off-failed", true);
                    }
                }

                break;
            }

            case ACTION_ALL_OFF: {
                if(!_fieldPowerKnown || !_fieldPowerOn) {
                    bool powerOK = runQueuedCommandWithRetries(
                        "all_off_power_on",
                        [this](VALVEMASTERCompletion completion) {
                            return _valveMaster.powerOn(completion);
                        },
                        10000);

                    if(!powerOK) {
                        _fieldPowerKnown = false;
                        _valveStateKnown = false;
                        _anyValveMayBeOpen = true;
                        _needPowerOnValveReset = true;

                        if(!_resultKey.empty()) {
                            setCachedValue(_resultKey, "all-off-power-on-failed", true);
                        }

                        break;
                    }

                    _fieldPowerKnown = true;
                    _fieldPowerOn = true;

                    if(!_powerKey.empty()) {
                        setCachedValue(_powerKey, "on", true);
                    }

                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(FIELD_BUS_NODE_STABILIZE_MS));
                }

                bool allOffOK = runQueuedCommandWithRetries(
                    "all_off",
                    [this](VALVEMASTERCompletion completion) {
                        return _valveMaster.allOff(completion);
                    },
                    10000);

                if(!allOffOK) {
                    _valveStateKnown = false;
                    _anyValveMayBeOpen = true;
                    _needPowerOnValveReset = true;

                    if(!_resultKey.empty()) {
                        setCachedValue(_resultKey, "all-off-failed", true);
                    }

                    break;
                }

                _valveStateKnown = true;
                _anyValveKnownOpen = false;
                _anyValveMayBeOpen = false;
                _needPowerOnValveReset = false;

                for(const auto& [key, binding] : _valveBindings) {
                    (void)binding;
                    setCachedValue(key, "0", true);
                }

                bool powerOffOK = runQueuedCommandWithRetries(
                    "all_off_power_off",
                    [this](VALVEMASTERCompletion completion) {
                        return _valveMaster.powerOff(completion);
                    },
                    10000);

                if(powerOffOK) {
                    _fieldPowerKnown = true;
                    _fieldPowerOn = false;
                    _needPowerOnValveReset = false;

                    if(!_powerKey.empty()) {
                        setCachedValue(_powerKey, "off", true);
                    }

                    if(!_resultKey.empty()) {
                        setCachedValue(_resultKey, "all-off-power-off-ok", true);
                    }
                } else {
                    _fieldPowerKnown = false;
                    _needPowerOnValveReset = false;

                    if(!_resultKey.empty()) {
                        setCachedValue(_resultKey, "all-off-ok-power-off-failed", true);
                    }
                }

                break;
            }

            case ACTION_SET_VALVE:
                (void)runSetValveAction(action);
                break;

            case ACTION_SET_ERROR: {
                bool ok = runQueuedCommandWithRetries(
                    "set_error",
                    [this](VALVEMASTERCompletion completion) {
                        return _valveMaster.setError(completion);
                    },
                    10000);

                if(ok) {
                    if(!_resultKey.empty()) {
                        setCachedValue(_resultKey, "set-error-ok", true);
                    }
                } else {
                    if(!_resultKey.empty()) {
                        setCachedValue(_resultKey, "set-error-failed", true);
                    }
                }

                break;
            }

            case ACTION_CLEAR_ERROR: {
                bool ok = runQueuedCommandWithRetries(
                    "clear_error",
                    [this](VALVEMASTERCompletion completion) {
                        return _valveMaster.clearError(completion);
                    },
                    10000);

                if(ok) {
                    if(!_resultKey.empty()) {
                        setCachedValue(_resultKey, "clear-error-ok", true);
                    }
                } else {
                    if(!_resultKey.empty()) {
                        setCachedValue(_resultKey, "clear-error-failed", true);
                    }
                }

                break;
            }

            default:
                LOGT_ERROR("VALVEMASTER_Device action \"%s\" has unknown type",
                           action.name.c_str());

                if(!_resultKey.empty()) {
                    setCachedValue(_resultKey, "action-error", true);
                }

                break;
        }
    }
}


/**
 * @brief Start the plugin.
 */
bool VALVEMASTER_Device::start()
{
    bool status = false;
    int error = 0;

    if(!_deviceProperties[PROP_ADDRESS].is_string()) {
        LOGT_ERROR("VALVEMASTER_Device begin called with no %s property",
                   string(PROP_ADDRESS).c_str());
        return false;
    }

    if(_deviceID.size() == 0) {
        LOGT_ERROR("VALVEMASTER_Device has no deviceID");
        return false;
    }

    parseRuntimeProperties();

    string address = _deviceProperties[PROP_ADDRESS];
    uint8_t i2cAddr = std::stoi(address.c_str(), 0, 16);

    if(!_isSetup) {
        LOGT_ERROR("VALVEMASTER_Device(%s) begin called before initWithSchema",
                   address.c_str());
        return false;
    }

    if(_isStarted && _valveMaster.isOpen()) {
        _deviceState = DEVICE_STATE_CONNECTED;
        return true;
    }

    LOGT_DEBUG("VALVEMASTER_Device(%02X) begin", i2cAddr);

    status = _valveMaster.begin(i2cAddr, error);

    if(!status) {
        LOGT_ERROR("VALVEMASTER_Device(%02X) begin FAILED: %s",
                   i2cAddr,
                   strerror(errno));

        _isStarted = false;
        _deviceState = DEVICE_STATE_ERROR;
        return false;
    }

    _masterVersion.clear();

    _fieldPowerKnown = false;
    _fieldPowerOn = false;

    _startupFieldPowerWasOn = false;

    _valveStateKnown = false;
    _anyValveKnownOpen = false;
    _anyValveMayBeOpen = false;

    _needPowerOnValveReset = true;

    cancelIdlePowerOff();

    detectStartupFieldPowerState();

    _startup_time = time(NULL);
    _isStarted = true;
    _deviceState = DEVICE_STATE_CONNECTED;

    if(_fieldPowerKnown && _fieldPowerOn) {
        if(syncConfiguredValveStatesFromHardware("startup-inherited-power")) {
            _needPowerOnValveReset = false;
        } else {
            _needPowerOnValveReset = true;
        }
    }

    startActionThread();
    seedStartupCache();

    LOGT_INFO("VALVEMASTER_Device(%02X) started, action thread running, cache reporting enabled, valveBindings=%zu queryDelay=%llu second(s), power_hold_sec=%u retry_count=%u retry_delay_ms=%u",
              i2cAddr,
              _valveBindings.size(),
              static_cast<unsigned long long>(_queryDelay),
              static_cast<unsigned int>(_powerHoldSec),
              static_cast<unsigned int>(_retryCount),
              static_cast<unsigned int>(_retryDelayMs));

    return true;
}


/**
 * @brief Stop the plugin.
 */
void VALVEMASTER_Device::stop()
{
    LOGT_DEBUG("VALVEMASTER_Device(%02X) stop", _valveMaster.getDevAddr());

    cancelIdlePowerOff();

    stopActionThread();

    if(_valveMaster.isOpen()) {

        if(_anyValveMayBeOpen) {
            LOGT_INFO("VALVEMASTER_Device stop: water risk exists, running synchronous allOff cleanup");

            if(!_fieldPowerKnown || !_fieldPowerOn) {
                bool powerOK = runQueuedCommandWithRetries(
                    "shutdown_power_on",
                    [this](VALVEMASTERCompletion completion) {
                        return _valveMaster.powerOn(completion);
                    },
                    10000);

                if(powerOK) {
                    _fieldPowerKnown = true;
                    _fieldPowerOn = true;

                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(FIELD_BUS_NODE_STABILIZE_MS));
                } else {
                    _fieldPowerKnown = false;
                    _needPowerOnValveReset = true;

                    LOGT_ERROR("VALVEMASTER_Device stop: shutdown_power_on failed; attempting allOff anyway");
                }
            }

            bool allOffOK = runQueuedCommandWithRetries(
                "shutdown_all_off",
                [this](VALVEMASTERCompletion completion) {
                    return _valveMaster.allOff(completion);
                },
                10000);

            if(allOffOK) {
                _valveStateKnown = true;
                _anyValveKnownOpen = false;
                _anyValveMayBeOpen = false;
                _needPowerOnValveReset = false;

                for(const auto& [key, binding] : _valveBindings) {
                    (void)binding;
                    setCachedValue(key, "0", true);
                }
            } else {
                _valveStateKnown = false;
                _anyValveMayBeOpen = true;
                _needPowerOnValveReset = true;

                LOGT_ERROR("VALVEMASTER_Device stop: shutdown_all_off failed");
            }
        } else {
            LOGT_DEBUG("VALVEMASTER_Device stop: no water risk marked");
        }

        if(_fieldPowerKnown && _fieldPowerOn) {
            bool powerOffOK = runQueuedCommandWithRetries(
                "shutdown_power_off",
                [this](VALVEMASTERCompletion completion) {
                    return _valveMaster.powerOff(completion);
                },
                10000);

            if(powerOffOK) {
                _fieldPowerKnown = true;
                _fieldPowerOn = false;
                _needPowerOnValveReset = true;

                if(!_powerKey.empty()) {
                    setCachedValue(_powerKey, "off", true);
                }

                if(!_resultKey.empty()) {
                    setCachedValue(_resultKey, "stop-power-off-ok", true);
                }
            } else {
                _fieldPowerKnown = false;
                _needPowerOnValveReset = true;

                LOGT_ERROR("VALVEMASTER_Device stop: shutdown_power_off failed");

                if(!_resultKey.empty()) {
                    setCachedValue(_resultKey, "stop-power-off-failed", true);
                }
            }
        }

        _valveMaster.stop();
    }

    _startup_time = 0;
    _isStarted = false;
    _deviceState = DEVICE_STATE_DISCONNECTED;
}


/**
 * @brief Enable or disable this device.
 */
bool VALVEMASTER_Device::setEnabled(bool enable)
{
    if(enable) {
        _isEnabled = true;

        if(_deviceState == DEVICE_STATE_CONNECTED) {
            return true;
        }

        stop();
        return start();
    }

    _isEnabled = false;

    if(_deviceState == DEVICE_STATE_CONNECTED) {
        stop();
    }

    return true;
}


/**
 * @brief Return whether the local VALVEMASTER I2C controller is connected.
 */
bool VALVEMASTER_Device::isConnected()
{
    if(!_isEnabled) {
        return false;
    }

    if(!_isStarted) {
        return false;
    }

    if(!_valveMaster.isOpen()) {
        _deviceState = DEVICE_STATE_DISCONNECTED;
        return false;
    }

    _deviceState = DEVICE_STATE_CONNECTED;
    return true;
}


/**
 * @brief Return whether there are pending cached values to report.
 */
bool VALVEMASTER_Device::hasUpdates()
{
    std::lock_guard<std::mutex> lock(_cacheMutex);
    return !_pendingValues.empty();
}


/**
 * @brief Drain pending cached values.
 */
bool VALVEMASTER_Device::getValues(keyValueMap_t &results)
{
    std::lock_guard<std::mutex> lock(_cacheMutex);

    if(_pendingValues.empty()) {
        return false;
    }

    results = _pendingValues;
    _pendingValues.clear();

    return !results.empty();
}


/**
 * @brief Runtime command path.
 */
bool VALVEMASTER_Device::deviceAction(string cmd)
{
    string normalized = normalizedActionCommand(cmd);

    if(normalized == "noop") {
        Action action;
        action.type = ACTION_NOOP;
        action.name = normalized;

        return enqueueAction(action);
    }

    if(normalized == "power_on") {
        Action action;
        action.type = ACTION_POWER_ON;
        action.name = normalized;

        if(!enqueueAction(action)) {
            return false;
        }

        return enqueueDelay(DELAY_NODE_STABILIZE, FIELD_BUS_NODE_STABILIZE_MS);
    }

    if(normalized == "power_off") {
        Action action;
        action.type = ACTION_POWER_OFF;
        action.name = normalized;

        return enqueueAction(action);
    }

    if(normalized == "all_off") {
        Action action;
        action.type = ACTION_ALL_OFF;
        action.name = normalized;

        return enqueueAction(action);
    }

    if(normalized == "set_error") {
        Action action;
        action.type = ACTION_SET_ERROR;
        action.name = normalized;

        return enqueueAction(action);
    }

    if(normalized == "clear_error") {
        Action action;
        action.type = ACTION_CLEAR_ERROR;
        action.name = normalized;

        return enqueueAction(action);
    }

    LOGT_ERROR("VALVEMASTER_Device deviceAction rejected: action \"%s\" is not enabled in runtime plugin",
               cmd.c_str());

    return false;
}

/**
 * @brief Runtime value-set path.
 */
bool VALVEMASTER_Device::setValues(keyValueMap_t kv)
{
    if(!_isEnabled || !_isStarted) {
        LOGT_ERROR("VALVEMASTER_Device setValues rejected: device is not started");
        return false;
    }

    if(kv.empty()) {
        LOGT_ERROR("VALVEMASTER_Device setValues rejected: empty request");
        return false;
    }

    vector<Action> actions;

    for(const auto& [key, textValue] : kv) {
        auto bindingIt = _valveBindings.find(key);

        if(bindingIt == _valveBindings.end()) {
            LOGT_ERROR("VALVEMASTER_Device setValues rejected: key \"%s\" is not a configured valve key",
                       key.c_str());
            return false;
        }

        bool desiredOpen = false;

        if(!parseValveBoolValue(textValue, desiredOpen)) {
            LOGT_ERROR("VALVEMASTER_Device setValues rejected: key \"%s\" has invalid value \"%s\"",
                       key.c_str(),
                       textValue.c_str());
            return false;
        }

        Action action;
        action.type = ACTION_SET_VALVE;
        action.name = "set_valve";
        action.key = key;
        action.node = bindingIt->second.node;
        action.channel = bindingIt->second.channel;
        action.desiredOpen = desiredOpen;

        actions.push_back(action);
    }

    cancelIdlePowerOff();

    for(const auto& action : actions) {
        setCachedValue(action.key, action.desiredOpen ? "1" : "0", false);

        if(action.desiredOpen) {
            _valveStateKnown = false;
            _anyValveKnownOpen = true;
            _anyValveMayBeOpen = true;
        } else {
            _valveStateKnown = false;
            _anyValveMayBeOpen = true;
        }

        if(!enqueueAction(action)) {
            LOGT_ERROR("VALVEMASTER_Device setValues failed to queue key \"%s\"",
                       action.key.c_str());
            return false;
        }
    }

    return true;
}
