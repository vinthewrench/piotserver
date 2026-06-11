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

#include <errno.h>
#include <string.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <climits>
#include <cstdint>
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
            { "setvalues_policy", "configured valve writes with read-back verification and queued-action coalescing" },
            { "runtime_start_power_on", "disabled" },
            { "runtime_start_who", "disabled" },
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

                    _valveBindings[key] = binding;

                    if(entry.queryDelay < delay) delay = entry.queryDelay;
                    _isSetup = true;

                    LOGT_DEBUG("VALVEMASTER_Device registered valve key \"%s\" node=%d channel=%d",
                               key.c_str(),
                               node,
                               channel);
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
 *   Later, the action thread verifies actual hardware truth. If actual truth
 *   matches the requested value, the cached value is the same, but the verified
 *   value still needs to be reported as hardware-confirmed truth.
 *
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
        /*
         * Do not overwrite a more specific startup valve-sync result.
         *
         * syncConfiguredValveStatesFromHardware() may already have reported:
         *
         *   startup-valve-sync-ok
         *   startup-valve-sync-failed
         *
         * If no specific result exists yet, report inherited field power or
         * normal startup none.
         */
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

    /*
     * Seed initial configured valve output values for pIoTServer/REST/UI.
     *
     * This is intentionally done only when field power is known off.
     *
     * This does not mean the physical latching valves are known closed. It only
     * gives pIoTServer an initial output/cache baseline for configured BOOL
     * keys so /values is not missing every SPRK_* key until the first command.
     *
     * Do not change:
     *
     *   _valveStateKnown
     *   _anyValveKnownOpen
     *   _anyValveMayBeOpen
     *   _needPowerOnValveReset
     *
     * Those safety/trust flags were already set by detectStartupFieldPowerState()
     * and must remain conservative.
     */
    if(_fieldPowerKnown && !_fieldPowerOn) {
        for(const auto& [key, binding] : _valveBindings) {
            (void)binding;
            setCachedValue(key, "0", true);
        }
    }

    /*
     * Do not seed _versionKey here.
     *
     * VALVEMASTER_VERSION is intended to mean controller firmware version.
     * Reading that would touch hardware beyond local open/start policy. A later
     * background diagnostic/action path can read the controller firmware and
     * populate this cache key.
     */
}


/**
 * @brief Parse runtime driver properties from _deviceProperties.
 *
 * Current parsed properties:
 *
 *   power_hold_sec
 *   retry_count
 *   retry_delay_ms
 *
 * power_hold_sec:
 *   post-close field-power hold timer. This does not hold latching valves open
 *   or closed.
 *
 * retry_count:
 *   total attempts, including the first attempt.
 *
 * retry_delay_ms:
 *   delay between failed attempts.
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
 *
 * This is a read-only local-controller probe.
 *
 * It must not:
 *
 *   - power on the field bus
 *   - power off the field bus
 *   - run WHO
 *   - scan nodes
 *   - close valves
 *   - allOff
 *   - touch valve nodes
 *   - provision anything
 *
 * Latching valve rule:
 *
 *   field power OFF does not prove valves are closed.
 *
 * Therefore, when startup finds field power off, the driver records that a
 * future power-on valve reset is required before trusted runtime valve
 * commands.
 */
void VALVEMASTER_Device::detectStartupFieldPowerState()
{
    uint8_t powerState = 0;

    /*
     * Preferred path: direct controller power-state register.
     *
     * Treat any non-zero value as field power on. This keeps the plugin
     * tolerant of firmware returning either 1 or a bitmask value.
     */
    if(_valveMaster.readFirmwarePowerState(powerState)) {
        _fieldPowerKnown = true;
        _fieldPowerOn = (powerState != 0);

        if(_fieldPowerOn) {
            _startupFieldPowerWasOn = true;

            /*
             * Field power was already on before this plugin owned runtime
             * truth. Node-reported state may be queried by start().
             */
            _valveStateKnown = false;
            _anyValveKnownOpen = false;
            _anyValveMayBeOpen = true;
            _needPowerOnValveReset = true;

            LOGT_DEBUG("VALVEMASTER_Device startup detected inherited field power ON using power-state register value=0x%02X",
                       static_cast<unsigned int>(powerState));
        } else {
            _startupFieldPowerWasOn = false;

            /*
             * Power off does not mean valves are physically closed. We cannot
             * query nodes while field power is off, so the next trusted valve
             * command must power on, stabilize, allOff, and then continue.
             *
             * Do not assert _anyValveMayBeOpen here, or normal clean startup
             * followed by stop() would power on and allOff every time.
             */
            _valveStateKnown = false;
            _anyValveKnownOpen = false;
            _anyValveMayBeOpen = false;
            _needPowerOnValveReset = true;

            LOGT_DEBUG("VALVEMASTER_Device startup detected field power OFF using power-state register value=0x%02X; power-on valve reset required before next trusted command",
                       static_cast<unsigned int>(powerState));
        }

        return;
    }

    /*
     * Fallback path: controller status register.
     *
     * This is still read-only. It does not change field power or node state.
     */
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

            LOGT_DEBUG("VALVEMASTER_Device startup detected inherited field power ON using status=0x%02X",
                       static_cast<unsigned int>(status));
        } else {
            _startupFieldPowerWasOn = false;

            _valveStateKnown = false;
            _anyValveKnownOpen = false;
            _anyValveMayBeOpen = false;
            _needPowerOnValveReset = true;

            LOGT_DEBUG("VALVEMASTER_Device startup detected field power OFF using status=0x%02X; power-on valve reset required before next trusted command",
                       static_cast<unsigned int>(status));
        }

        return;
    }

    /*
     * If the read fails, preserve unknown field-power state.
     *
     * Do not power anything here. Do not assert stop-time water risk from a
     * failed startup read alone. The next runtime valve command will perform a
     * power-on valve reset before trusting state.
     */
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
 *
 * VALVEMASTER wrapper calls such as powerOn()/powerOff() are asynchronous.
 * Their bool return value means the command was accepted/queued by the wrapper,
 * not that the hardware action completed successfully.
 *
 * This helper performs exactly one attempt. The retry wrapper calls this
 * helper repeatedly when retry policy is enabled.
 *
 * @param actionName Human-readable action name for logging.
 * @param submit Function that queues the wrapper action.
 * @param timeoutMs Completion timeout.
 * @return true if the command completed successfully.
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

                /*
                 * Use the wrapper's cached command outcome rather than guessing
                 * enum names here. The callback tells us completion happened;
                 * commandSucceeded() tells us whether the completed command
                 * succeeded according to the wrapper.
                 */
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
 *
 * A command succeeds as soon as any attempt succeeds.
 *
 * retry_count is total attempts, not retries-after-first.
 *
 * @param commandName Human-readable command name.
 * @param submit Function that queues the wrapper action.
 * @param timeoutMs Per-attempt timeout.
 * @return true if any attempt succeeded.
 */
bool VALVEMASTER_Device::runQueuedCommandWithRetries(
        const string& commandName,
        std::function<bool(VALVEMASTERCompletion)> submit,
        uint32_t timeoutMs)
{
    const uint32_t attempts = std::max<uint32_t>(1, _retryCount);

    for(uint32_t attempt = 1; attempt <= attempts; attempt++) {
        string attemptName = commandName;

        if(attempts > 1) {
            attemptName += "_attempt_";
            attemptName += std::to_string(attempt);
        }

        bool ok = runQueuedCommandAndWait(attemptName,
                                          submit,
                                          timeoutMs);

        if(ok) {
            if(attempt > 1) {
                LOGT_DEBUG("VALVEMASTER_Device command \"%s\" succeeded on attempt %u/%u",
                           commandName.c_str(),
                           static_cast<unsigned int>(attempt),
                           static_cast<unsigned int>(attempts));
            }

            return true;
        }

        LOGT_ERROR("VALVEMASTER_Device command \"%s\" failed attempt %u/%u",
                   commandName.c_str(),
                   static_cast<unsigned int>(attempt),
                   static_cast<unsigned int>(attempts));

        if(attempt < attempts && _retryDelayMs > 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(_retryDelayMs));
        }
    }

    return false;
}


/**
 * @brief Read one valve with retry policy.
 *
 * getValve() has a custom callback shape, so it cannot use
 * runQueuedCommandWithRetries() directly.
 *
 * This function validates that the reply node/channel match the requested
 * node/channel before accepting the read.
 *
 * @param reason Human-readable reason for logs.
 * @param key pIoTServer key.
 * @param node Node address.
 * @param channel Valve channel.
 * @param reportedOpen Receives node-reported commanded state.
 * @return true if read succeeded.
 */
bool VALVEMASTER_Device::readValveWithRetries(const string& reason,
                                              const string& key,
                                              uint8_t node,
                                              uint8_t channel,
                                              bool& reportedOpen)
{
    const uint32_t attempts = std::max<uint32_t>(1, _retryCount);

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
                    LOGT_DEBUG("VALVEMASTER_Device %s read valve key=\"%s\" succeeded on attempt %u/%u",
                               reason.c_str(),
                               key.c_str(),
                               static_cast<unsigned int>(attempt),
                               static_cast<unsigned int>(attempts));
                }

                return true;
            }
        }

        if(attempt < attempts && _retryDelayMs > 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(_retryDelayMs));
        }
    }

    return false;
}


/**
 * @brief Read all configured valve states from hardware and sync cache.
 *
 * This is a real field-bus/node read path.
 *
 * Policy:
 *
 *   - caller must ensure field power is already on
 *   - this function does not power on
 *   - this function does not power off
 *   - each configured valve binding is read through VALVEMASTER getValve()
 *   - cache receives node-reported commanded state as "1" or "0"
 *   - water-risk state is updated from node-reported commanded state
 *
 * Important:
 *
 *   VALVEMASTER nodes do not read true mechanical valve position. This sync
 *   reads node-reported commanded/logical state only.
 *
 * If any valve read fails, valve truth is not fully known and water risk
 * remains asserted.
 *
 * @param reason Human-readable reason for logging.
 * @return true if every configured valve was read successfully.
 */
bool VALVEMASTER_Device::syncConfiguredValveStatesFromHardware(const string& reason)
{
    if(_valveBindings.empty()) {
        LOGT_DEBUG("VALVEMASTER_Device valve sync skipped for \"%s\": no valve bindings",
                   reason.c_str());

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

    LOGT_DEBUG("VALVEMASTER_Device valve sync \"%s\" reading %zu configured valve(s)",
               reason.c_str(),
               _valveBindings.size());

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

        LOGT_DEBUG("VALVEMASTER_Device valve sync \"%s\" key \"%s\" node=%u channel=%u reported=%s",
                   reason.c_str(),
                   key.c_str(),
                   static_cast<unsigned int>(binding.node),
                   static_cast<unsigned int>(binding.channel),
                   valveOpen ? "open" : "closed");
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

    /*
     * If any configured valve read failed while field power is on, stay safe:
     * valve truth is incomplete and water may be running. The next trusted
     * valve command must perform power-on reset/allOff before proceeding.
     */
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
 *
 * This is the trust-establishing path for latching valves with no physical
 * valve position feedback.
 *
 * If field power is off or unknown, this powers the field bus on and waits for
 * node stabilization.
 *
 * If _needPowerOnValveReset is true, this sends allOff/close-all. On success,
 * all configured valves are cached as "0", water risk is cleared, and the
 * reset requirement is cleared.
 *
 * @param reason Human-readable reason for logging/result context.
 * @return true if field power is on and commanded-state trust is established.
 */
bool VALVEMASTER_Device::ensurePoweredAndResetIfNeeded(const string& reason)
{
    cancelIdlePowerOff();

    bool poweredOnThisCall = false;

    if(!_fieldPowerKnown || !_fieldPowerOn) {
        LOGT_DEBUG("VALVEMASTER_Device %s: powering field bus before trusted valve command",
                   reason.c_str());

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

            if(!_resultKey.empty()) {
                setCachedValue(_resultKey, "power-reset-power-on-failed", true);
            }

            return false;
        }

        _fieldPowerKnown = true;
        _fieldPowerOn = true;
        poweredOnThisCall = true;

        if(!_powerKey.empty()) {
            setCachedValue(_powerKey, "on", true);
        }
    }

    if(poweredOnThisCall) {
        LOGT_DEBUG("VALVEMASTER_Device %s: waiting %u ms for node stabilization",
                   reason.c_str(),
                   FIELD_BUS_NODE_STABILIZE_MS);

        std::this_thread::sleep_for(
            std::chrono::milliseconds(FIELD_BUS_NODE_STABILIZE_MS));
    }

    if(!_needPowerOnValveReset) {
        return true;
    }

    LOGT_DEBUG("VALVEMASTER_Device %s: running power-on valve reset allOff",
               reason.c_str());

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

        if(!_resultKey.empty()) {
            setCachedValue(_resultKey, "power-reset-all-off-failed", true);
        }

        return false;
    }

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
 *
 * Field valves are latching valves.
 *
 * Field-bus power is required to send commands and read node-reported
 * commanded state. Field-bus power is not required to hold a valve open or
 * closed after the command has been accepted and verified.
 *
 * Therefore this timer is allowed whenever field power is known on. It must
 * not require all configured valves to be closed.
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

    LOGT_DEBUG("VALVEMASTER_Device armed field-power hold timer for %u second(s)",
               static_cast<unsigned int>(_powerHoldSec));
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
 *
 * Caller must hold _actionMutex.
 *
 * @return true if timer is due.
 */
bool VALVEMASTER_Device::idlePowerOffDueLocked() const
{
    return
        _idlePowerOffArmed &&
        std::chrono::steady_clock::now() >= _idlePowerOffAt;
}


/**
 * @brief Run post-command field-power-off if the hold timer expires.
 *
 * This function is called only by the action thread after the field-power hold
 * timer expires and no queued action was available.
 *
 * Latching valve rule:
 *
 *   Field-bus power does not hold a valve open or closed.
 *
 * Therefore automatic timer-based power-off is allowed after a verified valve
 * command even if one or more valves are commanded open.
 *
 * Manual ACTION_POWER_OFF remains stricter because a direct user-requested
 * power_off is not a valve command and does not by itself establish commanded
 * valve state.
 *
 * @return true if no fatal driver error occurred.
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

    LOGT_DEBUG("VALVEMASTER_Device field-power hold expired, powering field bus off");

    bool powerOffOK = runQueuedCommandWithRetries(
        "idle_power_off",
        [this](VALVEMASTERCompletion completion) {
            return _valveMaster.powerOff(completion);
        },
        10000);

    if(powerOffOK) {
        _fieldPowerKnown = true;
        _fieldPowerOn = false;

        /*
         * Once field power has been removed, the next trusted valve command
         * must re-establish commanded-state trust:
         *
         *   power on
         *   wait for nodes
         *   allOff reset
         *   apply requested command
         *
         * This is true even though latching valves retain their commanded
         * mechanical state without field power.
         */
        _needPowerOnValveReset = true;

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
 * @brief Execute one valve set action and verify node-reported commanded state.
 *
 * This is called only by the serialized action thread.
 *
 * Policy:
 *
 *   - ensure field power is on
 *   - perform power-on valve reset if commanded-state trust is invalid
 *   - send SET_CHANNEL through VALVEMASTER wrapper
 *   - read the valve back using getValve()
 *   - cache node-reported commanded state
 *   - update commanded-state tracking
 *   - arm post-command field-power hold timer after verified command
 *
 * Important latching-valve rule:
 *
 *   Field-bus power does not hold a valve open or closed.
 *
 *   After a verified open command, the valve remains open without field-bus
 *   power. After a verified close command, the valve remains closed without
 *   field-bus power.
 *
 *   Therefore the field-power hold timer is armed after any successfully
 *   verified valve command, not only after all valves are closed.
 *
 * @param action ACTION_SET_VALVE record.
 * @return true if set and verification both succeeded.
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

    if(!ensurePoweredAndResetIfNeeded("set_valve")) {
        LOGT_ERROR("VALVEMASTER_Device set valve key=\"%s\" failed power/reset prerequisite",
                   action.key.c_str());

        if(!_resultKey.empty()) {
            setCachedValue(_resultKey, "set-valve-power-reset-failed", true);
        }

        return false;
    }

    /*
     * Send the requested valve command.
     */
    bool setOK = runQueuedCommandWithRetries(
        "set_valve",
        [this, &action](VALVEMASTERCompletion completion) {
            return _valveMaster.setValve(action.node,
                                         action.channel,
                                         action.desiredOpen,
                                         completion);
        },
        10000);

    if(!setOK) {
        LOGT_ERROR("VALVEMASTER_Device set valve failed key=\"%s\" node=%u channel=%u desired=%s",
                   action.key.c_str(),
                   static_cast<unsigned int>(action.node),
                   static_cast<unsigned int>(action.channel),
                   action.desiredOpen ? "open" : "closed");

        _valveStateKnown = false;
        _anyValveMayBeOpen = true;
        _needPowerOnValveReset = true;

        if(!_resultKey.empty()) {
            setCachedValue(_resultKey, "set-valve-failed", true);
        }

        return false;
    }

    /*
     * Read back node-reported commanded state.
     */
    bool actualOpen = false;

    if(!readValveWithRetries("set_valve_verify",
                             action.key,
                             action.node,
                             action.channel,
                             actualOpen)) {
        _valveStateKnown = false;
        _anyValveMayBeOpen = true;
        _needPowerOnValveReset = true;

        if(!_resultKey.empty()) {
            setCachedValue(_resultKey, "set-valve-verify-failed", true);
        }

        return false;
    }

    /*
     * Report node-reported commanded state, even if it disagrees with the
     * requested value. If pIoTServer already inserted the optimistic requested
     * value, this cache update becomes the correction path.
     */
    setCachedValue(action.key, actualOpen ? "1" : "0", true);

    /*
     * Recompute commanded-state tracking from cached configured valves.
     *
     * This is commanded/logical state only. VALVEMASTER nodes do not report
     * true mechanical valve position.
     */
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

    /*
     * Latching valve rule:
     *
     * Field-bus power is not required to hold the verified commanded state.
     * Arm the post-command field-power hold timer after any verified set,
     * whether the requested state was open or closed.
     */
    if(_fieldPowerKnown && _fieldPowerOn) {
        armIdlePowerOff();
    }

    if(actualOpen == action.desiredOpen) {
        if(!_resultKey.empty()) {
            setCachedValue(_resultKey, "set-valve-ok", true);
        }

        LOGT_DEBUG("VALVEMASTER_Device set valve verified key=\"%s\" node=%u channel=%u reported=%s",
                   action.key.c_str(),
                   static_cast<unsigned int>(action.node),
                   static_cast<unsigned int>(action.channel),
                   actualOpen ? "open" : "closed");

        return true;
    }

    /*
     * Node-reported state disagreed with requested value. Cache already
     * contains reported state, so report a correction condition.
     */
    if(!_resultKey.empty()) {
        setCachedValue(_resultKey, "set-valve-corrected", true);
    }

    LOGT_ERROR("VALVEMASTER_Device set valve corrected key=\"%s\" node=%u channel=%u requested=%s reported=%s",
               action.key.c_str(),
               static_cast<unsigned int>(action.node),
               static_cast<unsigned int>(action.channel),
               action.desiredOpen ? "open" : "closed",
               actualOpen ? "open" : "closed");

    return false;
}


/**
 * @brief Start the local action thread.
 *
 * The worker is used for asynchronous runtime actions. Starting it does not
 * touch irrigation hardware.
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

    LOGT_DEBUG("VALVEMASTER_Device action thread started");
}


/**
 * @brief Stop the local action thread.
 *
 * This flushes queued future work, signals shutdown, and joins the worker.
 *
 * If an action is already in flight, join waits for that action to finish or
 * reach its own timeout path. Queued but not-started actions are discarded.
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

    LOGT_DEBUG("VALVEMASTER_Device action thread stopped");
}


/**
 * @brief Remove queued valve set actions for one key.
 *
 * Caller must hold _actionMutex.
 *
 * Only queued-but-not-yet-running actions can be removed. An action already
 * popped by the worker is allowed to complete and verify normally.
 *
 * @param key Valve key.
 * @return Number of actions removed.
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
 *
 * Caller must hold _actionMutex.
 *
 * Used by all_off because all_off is a safety/superseding action. Pending
 * future opens/closes should not run after all_off has been requested.
 *
 * @return Number of actions removed.
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
 *
 * Valve set actions are coalesced by key:
 *
 *   A newer queued set for the same valve supersedes an older queued set that
 *   has not started yet.
 *
 * all_off is a superseding safety action:
 *
 *   It removes queued future valve set actions before all_off is queued.
 *
 * This does not cancel or interrupt an action already popped by the worker.
 *
 * @param action Action record.
 * @return true if accepted.
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

        /*
         * New runtime work takes precedence over an armed idle power-off timer.
         * If the timer was about to fire, the worker must process the explicit
         * action instead.
         */
        _idlePowerOffArmed = false;

        if(action.type == ACTION_SET_VALVE) {
            size_t removed =
                removeQueuedValveActionsForKeyLocked(action.key);

            if(removed > 0) {
                LOGT_DEBUG("VALVEMASTER_Device coalesced %zu queued valve action(s) for key \"%s\"",
                           removed,
                           action.key.c_str());
            }
        } else if(action.type == ACTION_ALL_OFF) {
            size_t removed = removeAllQueuedValveActionsLocked();

            if(removed > 0) {
                LOGT_DEBUG("VALVEMASTER_Device all_off removed %zu queued valve action(s)",
                           removed);
            }
        }

        _actionQueue.push_back(action);
    }

    _actionCv.notify_one();
    return true;
}


/**
 * @brief Queue a serialized delay action.
 *
 * @param name Human-readable delay name.
 * @param delayMs Delay duration in milliseconds.
 * @return true if accepted.
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
 *
 * Runtime hardware work is serialized here.
 *
 * The loop wakes for:
 *
 *   - queued actions
 *   - stop request
 *   - idle power-off timer expiration
 */
void VALVEMASTER_Device::actionThreadMain()
{
    LOGT_DEBUG("VALVEMASTER_Device action thread main enter");

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
                LOGT_DEBUG("VALVEMASTER_Device action noop");

                /*
                 * Cache-only path. Proves action queue -> worker -> reporting
                 * without touching hardware.
                 */
                if(!_resultKey.empty()) {
                    setCachedValue(_resultKey, "noop-ok", true);
                }

                break;

            case ACTION_DELAY:
                LOGT_DEBUG("VALVEMASTER_Device action delay \"%s\" %u ms",
                           action.name.c_str(),
                           action.delayMs);

                std::this_thread::sleep_for(
                    std::chrono::milliseconds(action.delayMs));

                if(action.name == DELAY_NODE_STABILIZE) {
                    /*
                     * power-on-ok means the controller power command completed
                     * and the serialized node-stabilize delay has elapsed.
                     */
                    if(!_resultKey.empty()) {
                        setCachedValue(_resultKey, "power-on-ok", true);
                    }
                }

                break;

            case ACTION_POWER_ON: {
                LOGT_DEBUG("VALVEMASTER_Device action power_on");

                /*
                 * Field-bus power action only.
                 *
                 * This does not run WHO, scan nodes, issue allOff, or touch
                 * valves. It only powers the VALVEMASTER-controlled field bus.
                 */
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

                    /*
                     * Do not set VALVEMASTER_RESULT = power-on-ok here.
                     *
                     * deviceAction("power_on") queues ACTION_POWER_ON followed
                     * by ACTION_DELAY("node-stabilize"). The delay action
                     * publishes power-on-ok after nodes have had time to boot.
                     */
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
                LOGT_DEBUG("VALVEMASTER_Device action power_off");

                /*
                 * power_off alone is not a water-safety action.
                 *
                 * Since valves are latching, powerOff does not close a valve.
                 * Reject explicit power_off while any valve may be open.
                 */
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
                LOGT_DEBUG("VALVEMASTER_Device action all_off");

                /*
                 * Safety primitive.
                 *
                 * all_off closes all VALVEMASTER field outputs. For this
                 * driver, all_off also drops field power afterward.
                 *
                 * enqueueAction() already removed queued future valve set
                 * actions. An already-running valve action is not interrupted.
                 */
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
                    /*
                     * If allOff fails, keep water-risk state asserted. We do
                     * not know that outputs are safe.
                     */
                    _valveStateKnown = false;
                    _anyValveMayBeOpen = true;
                    _needPowerOnValveReset = true;

                    if(!_resultKey.empty()) {
                        setCachedValue(_resultKey, "all-off-failed", true);
                    }

                    break;
                }

                /*
                 * The VALVEMASTER controller accepted allOff(). Reflect all
                 * configured valve outputs as closed.
                 */
                _valveStateKnown = true;
                _anyValveKnownOpen = false;
                _anyValveMayBeOpen = false;
                _needPowerOnValveReset = false;

                for(const auto& [key, binding] : _valveBindings) {
                    (void)binding;
                    setCachedValue(key, "0", true);
                }

                /*
                 * Drop field power after all outputs are closed.
                 */
                bool powerOffOK = runQueuedCommandWithRetries(
                    "all_off_power_off",
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
                        setCachedValue(_resultKey, "all-off-power-off-ok", true);
                    }
                } else {
                    /*
                     * Valves are believed closed, but field power may still be
                     * energized. Keep power state conservative.
                     */
                    _fieldPowerKnown = false;
                    _needPowerOnValveReset = true;

                    if(!_resultKey.empty()) {
                        setCachedValue(_resultKey, "all-off-ok-power-off-failed", true);
                    }
                }

                break;
            }

            case ACTION_SET_VALVE:
                LOGT_DEBUG("VALVEMASTER_Device action set_valve key=\"%s\" node=%u channel=%u desired=%s",
                           action.key.c_str(),
                           static_cast<unsigned int>(action.node),
                           static_cast<unsigned int>(action.channel),
                           action.desiredOpen ? "open" : "closed");

                (void)runSetValveAction(action);
                break;

            case ACTION_SET_ERROR: {
                LOGT_DEBUG("VALVEMASTER_Device action set_error");

                /*
                 * Staged diagnostic action.
                 *
                 * This intentionally asks the VALVEMASTER controller to enter
                 * its error/fault state so the plugin can prove diagnostic
                 * reporting and recovery flow. It does not touch valves.
                 */
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
                LOGT_DEBUG("VALVEMASTER_Device action clear_error");

                /*
                 * Staged diagnostic recovery action.
                 *
                 * Clears the controller error/fault state. This is a real
                 * runtime-safe recovery action and does not touch valves.
                 */
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

    LOGT_DEBUG("VALVEMASTER_Device action thread main exit");
}


/**
 * @brief Start the plugin.
 *
 * start() opens the local VALVEMASTER I2C controller, detects inherited
 * field-power state, optionally syncs configured node-reported valve states if
 * the field bus is already powered, starts the local action thread, and seeds
 * startup cache.
 *
 * start() must not:
 *
 *   - power on RS-485 field power
 *   - run WHO
 *   - scan nodes
 *   - close valves
 *   - allOff
 *   - powerOff
 *   - provision nodes
 *
 * @return true on local I2C open and worker start success.
 */
bool VALVEMASTER_Device::start()
{
    bool status = false;
    int error = 0;

    if(!_deviceProperties[PROP_ADDRESS].is_string()) {
        LOGT_DEBUG("VALVEMASTER_Device begin called with no %s property",
                   string(PROP_ADDRESS).c_str());
        return false;
    }

    if(_deviceID.size() == 0) {
        LOGT_DEBUG("VALVEMASTER_Device has no deviceID");
        return false;
    }

    parseRuntimeProperties();

    string address = _deviceProperties[PROP_ADDRESS];
    uint8_t i2cAddr = std::stoi(address.c_str(), 0, 16);

    if(!_isSetup) {
        LOGT_DEBUG("VALVEMASTER_Device(%s) begin called before initWithSchema",
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

    /*
     * If field power was already on before this plugin started, read
     * configured node-reported valve states. This is not physical position
     * feedback, but it is the best available runtime commanded-state truth for
     * an inherited powered session.
     */
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
 *
 * stop() means the app/plugin is going away.
 *
 * Runtime queue semantics no longer matter at shutdown. Safety does.
 *
 * Shutdown order:
 *
 *   1. Cancel idle power-off timer.
 *   2. Stop accepting queued runtime work.
 *   3. Flush queued future work.
 *   4. Let the current in-flight action finish or timeout.
 *   5. If water risk exists, synchronously make the field safe:
 *        - ensure field power
 *        - wait node stabilization if power had to be enabled
 *        - allOff
 *   6. If field power is known/on, synchronously power it off.
 *   7. Release local I2C.
 */
void VALVEMASTER_Device::stop()
{
    LOGT_DEBUG("VALVEMASTER_Device(%02X) stop", _valveMaster.getDevAddr());

    cancelIdlePowerOff();

    stopActionThread();

    if(_valveMaster.isOpen()) {

        if(_anyValveMayBeOpen) {
            LOGT_DEBUG("VALVEMASTER_Device stop: water risk exists, running synchronous allOff cleanup");

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

                    LOGT_DEBUG("VALVEMASTER_Device stop: waiting %u ms for node stabilization",
                               FIELD_BUS_NODE_STABILIZE_MS);

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
            LOGT_DEBUG("VALVEMASTER_Device stop: field power is on, running synchronous powerOff");

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
        } else {
            LOGT_DEBUG("VALVEMASTER_Device stop: field power not known on, skipping shutdown powerOff");
        }

        _valveMaster.stop();
    }

    _startup_time = 0;
    _isStarted = false;
    _deviceState = DEVICE_STATE_DISCONNECTED;
}


/**
 * @brief Enable or disable this device.
 *
 * Enabling restarts local I2C and the action thread if needed. Disabling stops
 * the worker and local I2C.
 *
 * @param enable true to enable.
 * @return true if final enabled/disabled state was reached.
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
 *
 * This does not mean the RS-485 field bus is powered or verified.
 *
 * @return true if enabled, started, and local I2C is open.
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
 *
 * pIoTServer calls this before getValues(). If this returns false, getValues()
 * will not be called by the manager read loop.
 *
 * @return true if _pendingValues is non-empty.
 */
bool VALVEMASTER_Device::hasUpdates()
{
    std::lock_guard<std::mutex> lock(_cacheMutex);
    return !_pendingValues.empty();
}


/**
 * @brief Drain pending cached values.
 *
 * This method is a reporting/cache-drain path only.
 *
 * It must not:
 *
 *   - touch hardware
 *   - power on the field bus
 *   - run WHO
 *   - read valve nodes
 *   - send valve commands
 *   - perform recovery
 *
 * @param results Receives changed cached values.
 * @return true if values were returned.
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
 *
 * Current implementation supports:
 *
 *   noop
 *   power_on
 *   power_off
 *   all_off
 *   set_error
 *   clear_error
 *
 * Individual valve commands are accepted through setValues(), not
 * deviceAction().
 *
 * Provisioning commands must never be added here.
 *
 * @param cmd Command string.
 * @return true if accepted.
 */
bool VALVEMASTER_Device::deviceAction(string cmd)
{
    if(cmd == "noop") {
        Action action;
        action.type = ACTION_NOOP;
        action.name = cmd;

        return enqueueAction(action);
    }

    if(cmd == "power_on") {
        Action action;
        action.type = ACTION_POWER_ON;
        action.name = cmd;

        /*
         * power-on success is not reported until after node stabilization.
         * Queueing the delay makes the timing part of the serialized action
         * stream rather than a hidden sleep inside ACTION_POWER_ON.
         */
        if(!enqueueAction(action)) {
            return false;
        }

        return enqueueDelay(DELAY_NODE_STABILIZE, FIELD_BUS_NODE_STABILIZE_MS);
    }

    if(cmd == "power_off") {
        Action action;
        action.type = ACTION_POWER_OFF;
        action.name = cmd;

        return enqueueAction(action);
    }

    if(cmd == "all_off") {
        Action action;
        action.type = ACTION_ALL_OFF;
        action.name = cmd;

        return enqueueAction(action);
    }

    if(cmd == "set_error") {
        Action action;
        action.type = ACTION_SET_ERROR;
        action.name = cmd;

        return enqueueAction(action);
    }

    if(cmd == "clear_error") {
        Action action;
        action.type = ACTION_CLEAR_ERROR;
        action.name = cmd;

        return enqueueAction(action);
    }

    LOGT_ERROR("VALVEMASTER_Device deviceAction rejected: action \"%s\" is not enabled in runtime plugin",
               cmd.c_str());

    return false;
}


/**
 * @brief Runtime value-set path.
 *
 * Accepts configured valve-state changes and queues serialized hardware work.
 *
 * Contract with pIoTServer:
 *
 *   - returning true means the request was accepted
 *   - manager may immediately insert requested values into the DB
 *   - action thread later verifies node-reported commanded state
 *   - cache is corrected/reported if node report differs
 *
 * Queue coalescing:
 *
 *   - each ACTION_SET_VALVE is coalesced by key in enqueueAction()
 *   - only queued-but-not-running actions can be removed
 *   - an active in-flight action is allowed to finish normally
 *
 * This function does not touch hardware directly.
 *
 * @param kv Requested key/value changes.
 * @return true if all requested keys were accepted and queued.
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

    /*
     * New valve work supersedes idle power-off timing.
     */
    cancelIdlePowerOff();

    /*
     * All validation succeeded. Now update internal requested state and queue.
     *
     * markPending=false because pIoTServer manager inserts requested values
     * immediately after setValues() returns true. The action thread will later
     * report verified actual node-reported state or corrections with
     * markPending=true.
     */
    for(const auto& action : actions) {
        setCachedValue(action.key, action.desiredOpen ? "1" : "0", false);

        if(action.desiredOpen) {
            _valveStateKnown = false;
            _anyValveKnownOpen = true;
            _anyValveMayBeOpen = true;
        } else {
            /*
             * A requested close is not node-reported truth yet. Until
             * verification completes, preserve water risk.
             */
            _valveStateKnown = false;
            _anyValveMayBeOpen = true;
        }

        if(!enqueueAction(action)) {
            LOGT_ERROR("VALVEMASTER_Device setValues failed to queue key \"%s\"",
                       action.key.c_str());
            return false;
        }
    }

    LOGT_DEBUG("VALVEMASTER_Device setValues accepted %zu valve request(s)",
               actions.size());

    return true;
}
