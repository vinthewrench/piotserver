#pragma once

/**
 * @file VALVEMASTER_Device.hpp
 * @brief pIoTServer plugin for the Valve Master I2C-to-RS-485 irrigation controller.
 *
 * The VALVEMASTER plugin is the Linux-side pIoTServer device driver for the
 * ATmega88PB-based Valve Master board.
 *
 * The plugin talks to the Valve Master board over I2C. The Valve Master board
 * then owns the field wiring side of the system:
 *
 *   - switching 12 V field-bus power
 *   - RS-485 transmit/receive direction control
 *   - ValveNode slave protocol timing
 *   - valve-node addressing
 *   - valve-channel command execution
 *
 * This plugin does not speak RS-485 directly. It only speaks the Valve Master
 * I2C register/command interface.
 *
 * Driver stack:
 *
 *   pIoTServer
 *     -> VALVEMASTER_Device plugin
 *       -> I2C register interface
 *         -> ATmega88PB Valve Master
 *           -> switched 12 V field-bus power
 *           -> RS-485 field bus
 *           -> ValveNode slave devices
 *             -> latching irrigation solenoids
 *
 * Threading model:
 *
 *   Public pIoTServer-facing calls should return quickly whenever practical.
 *   Slow hardware work is queued and handled by actionThread().
 *
 *   actionThread() owns serialized hardware operations, including:
 *
 *     - field-bus power on/off
 *     - Valve Master I2C command execution
 *     - ValveNode set/close/status verification commands
 *     - bus probing
 *     - node ping/version scans
 *     - auto power-off hold timing
 *
 *   This is important. I2C, field-power, and RS-485-related work should not be
 *   performed directly from REST handlers, sequence execution, or other command
 *   processor paths. Those paths should queue work for actionThread().
 *
 *   stop() is the exception. Application shutdown is allowed to block because
 *   it must make a best effort to close valves, shut down the field line, stop
 *   actionThread(), and leave hardware in a safe state before returning.
 *
 * Cached state warning:
 *
 *   Cached valve state in this driver means "last requested/desired state
 *   accepted by the driver." It does not prove:
 *
 *     - physical valve position
 *     - water flow
 *     - wiring continuity
 *     - solenoid presence
 *     - hydraulic success
 *     - completed field-bus command execution
 *
 *   Command verification improves confidence, but this driver still does not
 *   directly sense water movement or valve mechanics.
 */

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "pIoTServerDevice.hpp"
#include "pIoTServerSchema.hpp"
#include "I2C.hpp"
#include "CommonDefs.hpp"
#include "pIoTServerMgrCommon.hpp"

using namespace std;

class VALVEMASTER_Device : public pIoTServerDevice {
public:

    // MARK: - Valve Master DEVICE_ACTION command strings
    //
    // These strings are accepted through the generic pIoTServer sequence/action
    // command:
    //
    //     {
    //         "cmd": "DEVICE_ACTION",
    //         "key": "VALVEMASTER_1",
    //         "value": "ALL_OFF"
    //     }
    //
    // The sequence engine does not know what ALL_OFF, POWER_ON, or POWER_OFF
    // mean. It only routes the DEVICE_ACTION to the target device.
    //
    // The Valve Master driver owns the meaning of these action strings.
    //
    // DEVICE_ACTION is a live server command path. It is not the same thing as
    // application shutdown. These actions must queue work into the Valve Master
    // actionThread path, not perform I2C, field-power, or RS-485 work directly
    // from the sequence/command processor thread.
    //
    // Current actions:
    //
    //   ALL_OFF
    //     Request that all configured Valve Master channels be closed. In the
    //     current driver implementation, ACTION_CLOSE_ALL also cancels the power
    //     hold timer and powers the field bus off after close-all succeeds.
    //
    //   POWER_ON
    //     Request field-bus power on through the normal driver work queue.
    //
    //   POWER_OFF
    //     Request field-bus power off through the normal driver work queue.
    //
    // Abort-sequence note:
    //
    //   If a sequence uses ALL_OFF followed by POWER_OFF, both actions must be
    //   serialized through the same action queue so POWER_OFF cannot run before
    //   the close-all operation has been accepted or completed.
    //
    static constexpr std::string_view VALVEMASTER_ACTION_ALL_OFF   = "ALL_OFF";
    static constexpr std::string_view VALVEMASTER_ACTION_POWER_ON  = "POWER_ON";
    static constexpr std::string_view VALVEMASTER_ACTION_POWER_OFF = "POWER_OFF";

    // MARK: - Device configuration property names
    //
    // These are read from the device's JSON property block.
    //
    // Example:
    //
    //     {
    //         "device_type": "VALVEMASTER",
    //         "key": "VALVEMASTER_1",
    //         "address": "0x09",
    //         "power_hold_sec": 60
    //     }
    //
    static constexpr const char* JSON_ARG_ADDRESS = "address";
    static constexpr const char* JSON_ARG_POWER_HOLD_SEC = "power_hold_sec";
    static constexpr unsigned long DEFAULT_POWER_HOLD_SEC = 60;

    // MARK: - Diagnostic value keys
    //
    // These are additional diagnostic/status values exposed by the driver.
    // They are not physical valve channels. They report driver/field-bus state.
    //
    static constexpr const char* VALUE_FIELD_POWER = "field_power";
    static constexpr const char* VALUE_ACTION_BUSY = "action_busy";
    static constexpr const char* VALUE_LAST_ACTION = "last_action";
    static constexpr const char* VALUE_LAST_ACTION_OK = "last_action_ok";
    static constexpr const char* VALUE_DISCOVERED_NODE_COUNT = "discovered_node_count";
    static constexpr const char* VALUE_NODE_VERSIONS = "node_versions";

    /**
     * @brief Firmware version reported by a discovered ValveNode slave.
     */
    struct NodeVersion {
        uint8_t versionHi = 0;
        uint8_t versionLo = 0;
    };

    VALVEMASTER_Device(string devID, string driverName);
    virtual ~VALVEMASTER_Device();

    bool initWithSchema(deviceSchemaMap_t deviceSchema);
    bool start();
    void stop();
    bool isConnected();
    bool setEnabled(bool enable);
    bool setValues(keyValueMap_t kv);
    bool deviceAction(string cmd);
    bool hasUpdates();
    bool getValues(keyValueMap_t& results);
    bool allOff();
    bool getVersion(string& version);

    // MARK: - Manual/test entry points
    //
    // These are intended for harness/debug use. They should still respect the
    // same serialized hardware-access model as normal runtime actions.
    //
    bool testPowerOn();
    bool testPowerOff();
    bool testProbeBus();
    bool testPingDiscoveredNodes();
    bool testVersionScanDiscoveredNodes();
    bool testWaitForIdle(uint32_t timeoutMs);

private:

    /**
     * @brief Maps one pIoTServer value key to one ValveNode valve channel.
     *
     * Example:
     *
     *   SPRK_5 -> node 3, valve 1
     */
    struct ValveBinding {
        uint8_t node = 0;
        uint8_t valve = 0;
    };

    /**
     * @brief Internal serialized work items handled by actionThread().
     *
     * These are driver-private action types. They are not the same as the JSON
     * sequence action strings.
     */
    typedef enum
    {
        ACTION_NONE = 0,
        ACTION_SET_VALUES,
        ACTION_CLOSE_ALL,
        ACTION_POWER_ON,
        ACTION_POWER_OFF,
        ACTION_PROBE_BUS,
        ACTION_PING_DISCOVERED,
        ACTION_VERSION_SCAN_DISCOVERED
    } action_type_t;

    /**
     * @brief One queued request for actionThread().
     *
     * SET_VALUES uses the values vector.
     * Test/diagnostic actions usually only use type and optional callback.
     */
    struct action_request_t
    {
        action_type_t type = ACTION_NONE;
        keyBoolVector_t values;
        boolCallback_t callback = nullptr;
    };

    bool parseI2CAddress();
    bool loadBindingsFromSchema();
    bool getBindingForKey(const string& key, ValveBinding& bindingOut);

    const char* actionName(action_type_t type) const;
    void actionThread();
    bool queueAction(const action_request_t& request, bool clearPendingSetValues = false);
    bool executeAction(const action_request_t& request);
    bool executeSetValuesAction(const action_request_t& request);
    void armPowerHoldTimer();
    void cancelPowerHoldTimer();
    bool delayWithStopCheck(uint32_t delayMs, const char* reason);
    bool waitForIdle(uint32_t timeoutMs);
    void setLastActionStatus(const string& actionName, bool didSucceed);

    bool readRegister(uint8_t reg, uint8_t& valueOut);
    bool writeRegister(uint8_t reg, uint8_t value);
    bool writeCommand(uint8_t command);

    bool readMasterSummary();
    bool waitNotBusy(uint32_t timeoutMs);
    bool checkResultOk(const char* operation);

    bool powerOn();
    bool powerOff();
    bool ensureFieldPowerOn(bool& didPowerOnOut);
    bool settleAfterFieldPowerOnIfNeeded(bool didPowerOn, const char* reason);

    bool probeBus();
    bool pingNode(uint8_t node);
    bool pingDiscoveredNodes();
    bool getNodeVersion(uint8_t node, uint8_t& versionHiOut, uint8_t& versionLoOut);
    bool versionScanDiscoveredNodes();

    bool setValveChannel(uint8_t node, uint8_t channel, bool on);
    bool getValveChannelStatus(uint8_t node, uint8_t channel, bool& onOut);
    bool closeAllValves();

    void updateDiagnosticStateLocked();

    // MARK: - Schema and cached state

    deviceSchemaMap_t _schema;
    keyValueMap_t _state;
    map<string, ValveBinding> _bindings;

    bool _isSetup = false;
    bool _dataDidChange = false;

    mutable std::mutex _mutex;

    // MARK: - I2C / Valve Master state

    I2C _i2c;
    uint8_t _i2cAddress = 0x09;
    unsigned long _powerHoldSec = DEFAULT_POWER_HOLD_SEC;

    bool _isConnected = false;
    bool _fieldPowerOn = false;

    uint8_t _lastStatus = 0;
    uint8_t _lastResult = 0;
    uint8_t _lastPowerState = 0;
    uint8_t _lastNodeCount = 0;
    uint8_t _versionHi = 0;
    uint8_t _versionLo = 0;

    std::vector<uint8_t> _discoveredNodes;
    std::map<uint8_t, NodeVersion> _nodeVersions;

    // MARK: - Action thread state

    std::thread _thread;
    std::condition_variable _actionCv;
    std::deque<action_request_t> _actionQueue;

    bool _running = false;
    bool _stopRequested = false;
    bool _actionBusy = false;
    bool _powerHoldActive = false;

    std::chrono::steady_clock::time_point _powerHoldDeadline;

    string _lastActionName;
    bool _lastActionSucceeded = true;
};
