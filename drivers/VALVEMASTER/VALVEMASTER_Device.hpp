//
//  VALVEMASTER_Device.hpp
//  pIoTServer
//
//  Runtime VALVEMASTER pIoTServer plugin.
//
//  Hardware model:
//
//    - Field valves are latching valves.
//    - Field-bus power does not hold valves open or closed.
//    - Removing field-bus power does not close a valve.
//    - Nodes report commanded/logical valve state only.
//    - Nodes do not read true physical valve position.
//
//  Driver model:
//
//    - start() opens local I2C, detects field power, and optionally syncs
//      node-reported commanded state only if field power is already on.
//    - start() must not power on the field bus.
//    - If field power is off at startup, the next trusted valve command must
//      perform a power-on reset: powerOn, stabilize, allOff, then continue.
//    - setValues() accepts valve requests, queues action-thread work, and
//      later reports verified node-reported state.
//    - all_off closes all outputs and then powers the field bus off.
//    - power_hold_sec is a post-close field-power hold timer.
//    - retry_count / retry_delay_ms protect command-level transient failures.
//    - queued valve set actions are coalesced by key.
//    - all_off removes queued future valve set actions before it is queued.
//    - stop() cancels timers, stops queued work, allOffs if water risk exists,
//      powers off if field power is known on, and releases local I2C.
//

#ifndef VALVEMASTER_Device_hpp
#define VALVEMASTER_Device_hpp

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "pIoTServerDevice.hpp"
#include "VALVEMASTER.hpp"

using namespace std;

class VALVEMASTER_Device : public pIoTServerDevice {

public:

    /**
     * @brief Construct a VALVEMASTER plugin instance.
     *
     * This constructor is used by some local tests. It delegates to the
     * two-argument constructor.
     *
     * @param devID pIoTServer device ID.
     */
    VALVEMASTER_Device(string devID);

    /**
     * @brief Construct a VALVEMASTER plugin instance.
     *
     * Construction sets static pIoTServer properties only. It does not open I2C,
     * power the field bus, scan nodes, or touch valves.
     *
     * @param devID pIoTServer device ID.
     * @param driverName pIoTServer driver/plugin name.
     */
    VALVEMASTER_Device(string devID, string driverName);

    /**
     * @brief Destroy plugin instance.
     *
     * The destructor calls stop() so local I2C and worker resources are
     * released. stop() also performs safety cleanup if needed.
     */
    ~VALVEMASTER_Device();

    /**
     * @brief Return plugin version string.
     *
     * This is the pIoTServer plugin version, not necessarily the VALVEMASTER
     * controller firmware version.
     *
     * @param str Receives version string.
     * @return true.
     */
    bool getVersion(string &str);

    /**
     * @brief Initialize from pIoTServer schema.
     *
     * Records diagnostic keys and configured valve bindings. This method must
     * not touch hardware.
     *
     * Diagnostic keys:
     *
     *   VALVEMASTER_STATUS
     *   VALVEMASTER_POWER
     *   VALVEMASTER_RESULT
     *   VALVEMASTER_VERSION
     *
     * Valve binding shapes:
     *
     *   { "node": 1, "channel": 1 }
     *   { "node": 1, "valve": 1 }
     *   { "valve_node": 1, "valve_channel": 1 }
     *
     * @param deviceSchema pIoTServer device schema.
     * @return true if at least one supported key was found.
     */
    bool initWithSchema(deviceSchemaMap_t deviceSchema);

    /**
     * @brief Start the plugin.
     *
     * Opens local I2C, detects local field-power state, optionally syncs
     * configured node-reported valve state if field power is already on, starts
     * the action thread, and seeds startup cache values.
     *
     * start() must not power on the field bus.
     *
     * @return true on local I2C open and worker start success.
     */
    bool start();

    /**
     * @brief Stop the plugin.
     *
     * Cancels idle timer, stops the action thread, flushes queued future work,
     * performs synchronous allOff cleanup if water risk exists, powers off if
     * field power is known on, and releases local I2C.
     */
    void stop();

    /**
     * @brief Return whether local VALVEMASTER I2C controller is connected.
     *
     * This does not mean field-bus power is on and does not mean valve nodes
     * have been verified.
     *
     * @return true if enabled, started, and local I2C wrapper is open.
     */
    bool isConnected();

    /**
     * @brief Enable or disable this device.
     *
     * Enabling restarts the device if needed. Disabling stops it.
     *
     * @param enable true to enable.
     * @return true if final state was reached.
     */
    bool setEnabled(bool enable);

    /**
     * @brief Return whether cached values are waiting to be reported.
     *
     * pIoTServer calls hasUpdates() before getValues().
     *
     * @return true if pending values exist.
     */
    bool hasUpdates();

    /**
     * @brief Drain pending cached values.
     *
     * getValues() is cache-only. It must not touch hardware, power the field
     * bus, read nodes, or perform recovery.
     *
     * @param results Receives pending values.
     * @return true if values were returned.
     */
    bool getValues(keyValueMap_t &results);

    /**
     * @brief Queue an explicit runtime command.
     *
     * Supported commands:
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
     * Provisioning commands must not be added here.
     *
     * @param cmd Runtime command string.
     * @return true if accepted.
     */
    bool deviceAction(string cmd);

    /**
     * @brief Accept runtime valve state requests.
     *
     * Validates configured valve keys, stores requested values internally
     * without reporting them pending, coalesces queued future valve actions by
     * key, queues hardware work, and returns promptly.
     *
     * The action thread later reports verified node-reported state.
     *
     * @param kv Requested key/value changes.
     * @return true if all requested changes were accepted and queued.
     */
    bool setValues(keyValueMap_t kv);

private:

    /**
     * @brief Internal action types processed by the action thread.
     *
     * ACTION_SET_VALVE actions are eligible for queue coalescing.
     *
     * ACTION_ALL_OFF supersedes queued future ACTION_SET_VALVE actions.
     *
     * Other actions are not coalesced.
     */
    enum ActionType {
        ACTION_NOOP = 0,
        ACTION_DELAY,
        ACTION_POWER_ON,
        ACTION_POWER_OFF,
        ACTION_ALL_OFF,
        ACTION_SET_VALVE,
        ACTION_SET_ERROR,
        ACTION_CLEAR_ERROR
    };

    /**
     * @brief Queued action record.
     *
     * delayMs is used by ACTION_DELAY.
     *
     * key/node/channel/desiredOpen are used by ACTION_SET_VALVE.
     */
    struct Action {
        ActionType type = ACTION_NOOP;
        string     name;
        uint32_t   delayMs = 0;

        string     key;
        uint8_t    node = 0;
        uint8_t    channel = 0;
        bool       desiredOpen = false;
    };

    /**
     * @brief Mapping from pIoTServer key to physical node/channel.
     *
     * Example:
     *
     *   SPRK_1 -> node 1, channel 1
     */
    struct ValveBinding {
        uint8_t node = 0;
        uint8_t channel = 0;
    };

    /**
     * @brief Update cached value and optionally mark it pending.
     *
     * markPending=true reports the value through hasUpdates()/getValues().
     *
     * markPending=false updates the internal requested-state cache only.
     *
     * @param key pIoTServer key.
     * @param value String value.
     * @param markPending Whether to report through getValues().
     */
    void setCachedValue(const string& key,
                        const string& value,
                        bool markPending = true);

    /**
     * @brief Seed startup-visible cached values after start().
     *
     * This publishes already-known local/plugin state only. It must not touch
     * field-bus hardware.
     */
    void seedStartupCache();

    /**
     * @brief Parse runtime driver properties.
     *
     * Current properties:
     *
     *   power_hold_sec
     *   retry_count
     *   retry_delay_ms
     */
    void parseRuntimeProperties();

    /**
     * @brief Detect local controller field-power state at startup.
     *
     * This is read-only. It must not power on, power off, allOff, scan nodes,
     * or provision.
     */
    void detectStartupFieldPowerState();

    /**
     * @brief Sync configured node-reported commanded valve states.
     *
     * Caller must ensure field power is already on. This function does not
     * power the field bus on or off.
     *
     * @param reason Human-readable log context.
     * @return true if every configured valve read succeeded.
     */
    bool syncConfiguredValveStatesFromHardware(const string& reason);

    /**
     * @brief Ensure field power and commanded-state trust.
     *
     * Powers field bus on if needed, waits for node stabilization if power was
     * just enabled, and performs allOff reset when commanded-state trust is
     * invalid.
     *
     * @param reason Human-readable log/result context.
     * @return true if trusted command baseline exists.
     */
    bool ensurePoweredAndResetIfNeeded(const string& reason);

    /**
     * @brief Run one queued valve set action.
     *
     * Ensures field power/reset, sends set command with retries, verifies with
     * getValve retries, updates cache, updates water-risk state, and arms idle
     * power-off when all valves are known closed.
     *
     * @param action ACTION_SET_VALVE record.
     * @return true on successful set and verification.
     */
    bool runSetValveAction(const Action& action);

    /**
     * @brief Arm post-close field-power idle timer.
     *
     * Timer may only arm when field power is known on, valve state is known,
     * and no valve may be open.
     */
    void armIdlePowerOff();

    /**
     * @brief Cancel post-close field-power idle timer.
     */
    void cancelIdlePowerOff();

    /**
     * @brief Return whether idle timer is armed and due.
     *
     * Caller must hold _actionMutex.
     *
     * @return true if idle timer is due.
     */
    bool idlePowerOffDueLocked() const;

    /**
     * @brief Run idle power-off if still safe.
     *
     * Called by action thread after the idle timer expires and queue is empty.
     *
     * @return true if no fatal driver error occurred.
     */
    bool runIdlePowerOff();

    /**
     * @brief Start local serialized action worker.
     */
    void startActionThread();

    /**
     * @brief Stop local serialized action worker and flush queued future work.
     */
    void stopActionThread();

    /**
     * @brief Main action worker loop.
     *
     * Processes queued actions, shutdown signal, and idle power-off timer.
     */
    void actionThreadMain();

    /**
     * @brief Queue an action.
     *
     * ACTION_SET_VALVE:
     *   removes older queued ACTION_SET_VALVE actions for the same key before
     *   appending the newer request.
     *
     * ACTION_ALL_OFF:
     *   removes all queued ACTION_SET_VALVE actions before appending all_off.
     *
     * Other action types:
     *   appended without coalescing.
     *
     * @param action Action to queue.
     * @return true if accepted.
     */
    bool enqueueAction(const Action& action);

    /**
     * @brief Queue a serialized delay action.
     *
     * Used for node stabilization and future timing gaps.
     *
     * @param name Delay label.
     * @param delayMs Delay in milliseconds.
     * @return true if accepted.
     */
    bool enqueueDelay(const string& name, uint32_t delayMs);

    /**
     * @brief Remove queued valve actions for one key.
     *
     * Used by setValues() coalescing so a newer requested state for the same
     * valve supersedes older queued-but-not-yet-running state.
     *
     * Caller must hold _actionMutex.
     *
     * @param key pIoTServer valve key.
     * @return Number of queued actions removed.
     */
    size_t removeQueuedValveActionsForKeyLocked(const string& key);

    /**
     * @brief Remove all queued valve set actions.
     *
     * Used by all_off because all_off is a safety/superseding action.
     *
     * Caller must hold _actionMutex.
     *
     * @return Number of queued actions removed.
     */
    size_t removeAllQueuedValveActionsLocked();

    /**
     * @brief Submit one VALVEMASTER async command and wait for completion.
     *
     * This performs exactly one attempt.
     *
     * @param actionName Human-readable command name.
     * @param submit Lambda that starts the wrapper command.
     * @param timeoutMs Completion timeout.
     * @return true if the command completed successfully.
     */
    bool runQueuedCommandAndWait(const string& actionName,
                                 std::function<bool(VALVEMASTERCompletion)> submit,
                                 uint32_t timeoutMs = 10000);

    /**
     * @brief Run one queued command with retry policy.
     *
     * retry_count is the total number of attempts including the first attempt.
     *
     * @param commandName Human-readable command name.
     * @param submit Lambda that starts the wrapper command.
     * @param timeoutMs Per-attempt timeout.
     * @return true if any attempt succeeded.
     */
    bool runQueuedCommandWithRetries(const string& commandName,
                                     std::function<bool(VALVEMASTERCompletion)> submit,
                                     uint32_t timeoutMs = 10000);

    /**
     * @brief Read one valve with retry policy.
     *
     * getValve() has a richer callback shape than the simple command
     * completion path, so it has a dedicated retry wrapper.
     *
     * @param reason Human-readable log context.
     * @param key pIoTServer valve key.
     * @param node Node address.
     * @param channel Node-local valve channel.
     * @param reportedOpen Receives node-reported commanded state.
     * @return true if read succeeded with matching node/channel response.
     */
    bool readValveWithRetries(const string& reason,
                              const string& key,
                              uint8_t node,
                              uint8_t channel,
                              bool& reportedOpen);

    /**
     * @brief Setup/lifecycle state.
     */
    bool        _isSetup = false;
    bool        _isStarted = false;

    /**
     * @brief Startup time and schema query delay.
     *
     * getValues() is cache-drain only; _queryDelay is retained for manager
     * policy and future background diagnostics.
     */
    time_t      _startup_time = 0;
    uint64_t    _queryDelay = 0;

    /**
     * @brief Post-close field-power hold time in seconds.
     *
     * This is not a valve hold timer. Valves are latching.
     *
     * 0 means power off as soon as the queue is empty and all valves are known
     * closed.
     */
    uint32_t    _powerHoldSec = 60;

    /**
     * @brief Total command attempts for retry-protected operations.
     *
     * Includes the first attempt.
     */
    uint32_t    _retryCount = 3;

    /**
     * @brief Delay between failed retry attempts.
     */
    uint32_t    _retryDelayMs = 150;

    /**
     * @brief Idle power-off timer state.
     *
     * Protected by _actionMutex.
     */
    bool        _idlePowerOffArmed = false;
    std::chrono::steady_clock::time_point _idlePowerOffAt;

    /**
     * @brief Local VALVEMASTER I2C address.
     *
     * Farm default is 0x09.
     */
    uint8_t     _i2cAddress = 0x09;

    /**
     * @brief Optional diagnostic keys from schema.
     */
    string      _statusKey;
    string      _powerKey;
    string      _resultKey;
    string      _versionKey;

    /**
     * @brief Reserved controller firmware version cache.
     *
     * This is not the plugin version returned by getVersion().
     */
    string      _masterVersion;

    /**
     * @brief Configured valve bindings.
     */
    std::map<string, ValveBinding> _valveBindings;

    /**
     * @brief Field power state known by the driver.
     */
    bool        _fieldPowerKnown = false;
    bool        _fieldPowerOn = false;

    /**
     * @brief True if start() found field power already on.
     *
     * This means field power was inherited from a prior runtime/test/app.
     */
    bool        _startupFieldPowerWasOn = false;

    /**
     * @brief Commanded valve state trust/risk tracking.
     *
     * _valveStateKnown:
     *   true only after successful sync, verification, or allOff baseline.
     *
     * _anyValveKnownOpen:
     *   true if at least one configured valve is known/reported open.
     *
     * _anyValveMayBeOpen:
     *   conservative shutdown/safety trigger.
     */
    bool        _valveStateKnown = false;
    bool        _anyValveKnownOpen = false;
    bool        _anyValveMayBeOpen = false;

    /**
     * @brief Whether next trusted valve command must first run allOff reset.
     *
     * Set true after field power has been off, failed sync, failed operation, or
     * any other state that invalidates commanded-state trust.
     */
    bool        _needPowerOnValveReset = true;

    /**
     * @brief Cached values and pending reported values.
     */
    mutable std::mutex _cacheMutex;
    keyValueMap_t      _cachedValues;
    keyValueMap_t      _pendingValues;

    /**
     * @brief Serialized action worker state.
     *
     * _actionMutex protects:
     *
     *   - _actionQueue
     *   - _actionThreadRunning
     *   - _stopRequested
     *   - idle power-off timer state
     */
    std::mutex              _actionMutex;
    std::condition_variable _actionCv;
    std::deque<Action>      _actionQueue;
    std::thread             _actionThread;
    bool                    _actionThreadRunning = false;
    bool                    _stopRequested = false;

    /**
     * @brief Low-level VALVEMASTER wrapper.
     */
    VALVEMASTER _valveMaster;
};

#endif /* VALVEMASTER_Device_hpp */
