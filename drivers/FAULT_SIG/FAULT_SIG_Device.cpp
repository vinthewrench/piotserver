//
//  FAULT_SIG_Device.cpp
//  pIoTServer
//
//  Runtime FAULT_SIG pIoTServer plugin.
//
//  This plugin monitors the farm hardware fault signal line through the
//  shared GPIO helper class. It exposes the configured FAULT_SIG_ACTIVE key
//  and raises/clears IncidentMgr records on meaningful state transitions.
//
//  Hardware policy:
//
//    - FAULT_SIG is a singleton device.
//    - The GPIO line is fixed at GPIO22 for now.
//    - The signal is active-low by default.
//    - GPIO high means no fault when active_low=true.
//    - GPIO low means fault active when active_low=true.
//
//  Runtime design:
//
//    - initWithSchema() discovers the FAULT_SIG_ACTIVE key and query delay.
//    - start() opens GPIO22 as an input.
//    - getValues() reads the GPIO line and reports FAULT_SIG_ACTIVE.
//    - IncidentMgr records read failures and fault-active transitions.
//    - stop() releases the GPIO line.
//
//  Current schema:
//
//    {
//        "device_type": "FAULT_SIG",
//        "deviceID": "FAULT_SIG",
//        "enable": true,
//        "interval": 1,
//        "pins": [
//            {
//                "data_type": "BOOL",
//                "key": "FAULT_SIG_ACTIVE",
//                "other.props": {
//                    "active_low": true
//                },
//                "read_only": true,
//                "title": "Fault Signal Active",
//                "tracking": "track.changes"
//            }
//        ],
//        "title": "Fault Signal"
//    }
//

#include "FAULT_SIG_Device.hpp"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <limits>

#include "IncidentMgr.hpp"
#include "LogMgr.hpp"

bool FAULT_SIG_Device::_instanceExists = false;


/**
 * @brief Construct the FAULT_SIG plugin instance.
 *
 * The constructor records the pIoTServer device identity and enforces the
 * singleton rule. It does not open GPIO hardware. GPIO setup is deferred to
 * start() after initWithSchema() has registered the configured value key.
 *
 * @param devID pIoTServer device ID.
 * @param driverName pIoTServer driver/plugin name.
 */
FAULT_SIG_Device::FAULT_SIG_Device(std::string devID, std::string driverName)
{
    setDeviceID(devID, driverName);

    _deviceState = DEVICE_STATE_UNKNOWN;
    _isSetup = false;

    if(_instanceExists) {
        LOGT_ERROR("FAULT_SIG_Device only supports one instance");
        _deviceState = DEVICE_STATE_ERROR;
        return;
    }

    _instanceExists = true;
}

/**
 * @brief Destroy the FAULT_SIG plugin instance.
 *
 * Releases GPIO resources and clears the singleton guard.
 */
FAULT_SIG_Device::~FAULT_SIG_Device()
{
    stop();
    _instanceExists = false;
}

/**
 * @brief Return the FAULT_SIG plugin version.
 *
 * Uses the build-provided FAULT_SIG_DRIVER_VERSION macro when present.
 * Falls back to the driver name for older/simple builds.
 *
 * @param version Receives version string.
 * @return true.
 */
bool FAULT_SIG_Device::getVersion(std::string &version)
{
#ifdef FAULT_SIG_DRIVER_VERSION
    version = FAULT_SIG_DRIVER_VERSION;
#else
    version = "FAULT_SIG";
#endif
    return true;
}

/**
 * @brief Initialize the driver from pIoTServer schema.
 *
 * Discovers the configured FAULT_SIG_ACTIVE key and records the shortest
 * queryDelay supplied by the schema.
 *
 * FAULT_SIG is intentionally a singleton. GPIO remains fixed at GPIO22 for
 * now. The only supported per-key schema override is signal polarity:
 *
 * @code{.json}
 * "other.props": {
 *     "active_low": true
 * }
 * @endcode
 *
 * @param deviceSchema pIoTServer device schema entries for this plugin.
 * @return true if the FAULT_SIG_ACTIVE key was found and accepted.
 */
bool FAULT_SIG_Device::initWithSchema(deviceSchemaMap_t deviceSchema)
{
    uint64_t delay = UINT64_MAX;

    _faultSigActiveKey.clear();

    _gpioLine = default_gpioLine;
    _activeLow = default_activeLow;
    _queryDelay = default_queryDelay;
    _isSetup = false;

    for(const auto& [key, entry] : deviceSchema) {

        if(key == KEY_FAULT_SIG_ACTIVE) {
            _faultSigActiveKey = key;

            if(entry.queryDelay < delay) {
                delay = entry.queryDelay;
            }

            /*
             * entry.otherProps is nlohmann::json here.
             *
             * Keep GPIO fixed for now because FAULT_SIG is a singleton.
             * Allow only polarity override:
             *
             *   "other.props": {
             *      "active_low": true
             *   }
             */
            if(entry.otherProps.contains("active_low")) {
                try {
                    _activeLow = entry.otherProps.at("active_low").get<bool>();
                }
                catch(...) {
                    LOGT_ERROR("FAULT_SIG_Device devID \"%s\" invalid active_low value",
                               _deviceID.c_str());
                    _deviceState = DEVICE_STATE_ERROR;
                    return false;
                }
            }

            _isSetup = true;
        }
        else {
            LOGT_ERROR("FAULT_SIG_Device ignored unsupported key \"%s\"",
                       key.c_str());
        }
    }

    _queryDelay = delay != UINT64_MAX ? delay : default_queryDelay;
    _deviceState = DEVICE_STATE_DISCONNECTED;

    LOGT_DEBUG("FAULT_SIG_Device devID \"%s\" setup=%s faultSigActiveKey=\"%s\" gpio=%d activeLow=%s queryDelay=%llu",
               _deviceID.c_str(),
               _isSetup ? "true" : "false",
               _faultSigActiveKey.c_str(),
               _gpioLine,
               _activeLow ? "true" : "false",
               static_cast<unsigned long long>(_queryDelay));

    return _isSetup;
}

/**
 * @brief Start the FAULT_SIG plugin.
 *
 * Opens the configured GPIO line as an input through the shared GPIO helper.
 * This method does not read or report the line state; getValues() performs
 * the actual polling.
 *
 * @return true if GPIO setup succeeds.
 */
bool FAULT_SIG_Device::start()
{
    if(!_isSetup) {
        LOGT_ERROR("FAULT_SIG_Device devID \"%s\" start failed: device is not setup",
                   _deviceID.c_str());
        _deviceState = DEVICE_STATE_ERROR;
        return false;
    }

    int error = 0;

    GPIO::gpio_pin_t faultPin;
    faultPin.lineNo = static_cast<unsigned int>(_gpioLine);
    faultPin.direction = GPIO::GPIO_DIRECTION_INPUT;
    faultPin.flags = GPIO_V2_LINE_FLAG_INPUT;

    if(!_gpio.begin({ faultPin }, error)) {
        LOGT_ERROR("FAULT_SIG_Device devID \"%s\" failed to open GPIO line %d: %s",
                   _deviceID.c_str(),
                   _gpioLine,
                   strerror(error));

        _deviceState = DEVICE_STATE_ERROR;
        return false;
    }

    gettimeofday(&_lastQueryTime, NULL);
    _deviceState = DEVICE_STATE_CONNECTED;

    LOGT_INFO("FAULT_SIG_Device devID \"%s\" started gpio=%d active_low=%s queryDelay=%llu",
              _deviceID.c_str(),
              _gpioLine,
              _activeLow ? "true" : "false",
              static_cast<unsigned long long>(_queryDelay));

    return true;
}

/**
 * @brief Stop the FAULT_SIG plugin.
 *
 * Releases GPIO resources. If the device is already in DEVICE_STATE_ERROR,
 * the error state is preserved for diagnostics.
 */
void FAULT_SIG_Device::stop()
{
    _gpio.stop();

    if(_deviceState != DEVICE_STATE_ERROR) {
        _deviceState = DEVICE_STATE_DISCONNECTED;
    }
}

/**
 * @brief Enable or disable this device.
 *
 * Enabling starts the GPIO input if needed. Disabling releases it.
 *
 * @param enable true to enable, false to disable.
 * @return true if the requested state was reached.
 */
bool FAULT_SIG_Device::setEnabled(bool enable)
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

/**
 * @brief Return whether the FAULT_SIG GPIO input is ready.
 *
 * @return true if the device is enabled, connected, and GPIO is available.
 */
bool FAULT_SIG_Device::isConnected()
{
    return (_isEnabled && _deviceState == DEVICE_STATE_CONNECTED && _gpio.isAvailable());
}

/**
 * @brief Read and publish the fault signal state.
 *
 * Reads GPIO22 through the shared GPIO helper, converts the raw electrical
 * level into a logical FAULT_SIG_ACTIVE value using the configured polarity,
 * and writes the configured key into results.
 *
 * Incident behavior:
 *
 *   - GPIO read failure raises FAULT_SIG_READ_FAILED.
 *   - Missing GPIO line in the read result raises FAULT_SIG_READ_FAILED.
 *   - Successful read clears FAULT_SIG_READ_FAILED.
 *   - Fault inactive -> active raises FAULT_SIG_ACTIVE.
 *   - Fault active -> inactive clears FAULT_SIG_ACTIVE.
 *
 * Repeated unchanged fault states do not spam IncidentMgr; only transitions
 * raise/clear the active fault incident.
 *
 * @param results Receives pIoTServer key/value output.
 * @return true if the GPIO line was read successfully.
 */
bool FAULT_SIG_Device::getValues(keyValueMap_t &results)
{
    if(!isConnected()) {
        return false;
    }

    GPIO::gpioStates_t states;

    if(!_gpio.get(states)) {
        LOGT_ERROR("FAULT_SIG_Device devID \"%s\" failed reading GPIO line %d",
                   _deviceID.c_str(),
                   _gpioLine);

        _deviceState = DEVICE_STATE_ERROR;

        std::string details =
            "gpio=" + std::to_string(_gpioLine) +
            " error=gpio_read_failed";

        IncidentMgr::shared()->raise(
            _deviceID,
            IncidentMgr::Severity::Error,
            "FAULT_SIG_READ_FAILED",
            _faultSigActiveKey.empty() ? KEY_FAULT_SIG_ACTIVE : _faultSigActiveKey,
            "Fault signal GPIO read failed",
            details.c_str()
        );

        return false;
    }

    bool rawHigh = false;
    bool found = false;

    for(const auto &state : states) {
        if(state.first == _gpioLine) {
            rawHigh = state.second;
            found = true;
            break;
        }
    }

    if(!found) {
        LOGT_ERROR("FAULT_SIG_Device devID \"%s\" GPIO line %d not found in read result",
                   _deviceID.c_str(),
                   _gpioLine);

        _deviceState = DEVICE_STATE_ERROR;

        std::string details =
            "gpio=" + std::to_string(_gpioLine) +
            " error=gpio_line_missing";

        IncidentMgr::shared()->raise(
            _deviceID,
            IncidentMgr::Severity::Error,
            "FAULT_SIG_READ_FAILED",
            _faultSigActiveKey.empty() ? KEY_FAULT_SIG_ACTIVE : _faultSigActiveKey,
            "Fault signal GPIO line missing from read result",
            details.c_str()
        );

        return false;
    }

    IncidentMgr::shared()->clear(
        _deviceID,
        "FAULT_SIG_READ_FAILED",
        _faultSigActiveKey.empty() ? KEY_FAULT_SIG_ACTIVE : _faultSigActiveKey
    );

    _faultActive = _activeLow ? !rawHigh : rawHigh;

    if(!_faultSigActiveKey.empty()) {
        results[_faultSigActiveKey] = _faultActive ? "1" : "0";
    }

    if(!_hasLastFaultState || _faultActive != _lastFaultActive) {
        LOGT_INFO("FAULT_SIG_Device devID \"%s\" %s=%s gpio=%d raw=%s active_low=%s",
                  _deviceID.c_str(),
                  _faultSigActiveKey.c_str(),
                  _faultActive ? "true" : "false",
                  _gpioLine,
                  rawHigh ? "high" : "low",
                  _activeLow ? "true" : "false");

        std::string details =
            "gpio=" + std::to_string(_gpioLine) +
            " raw=" + std::string(rawHigh ? "high" : "low") +
            " active_low=" + std::string(_activeLow ? "true" : "false");

        if(_faultActive) {
            IncidentMgr::shared()->raise(
                _deviceID,
                IncidentMgr::Severity::Error,
                "FAULT_SIG_ACTIVE",
                _faultSigActiveKey.empty() ? KEY_FAULT_SIG_ACTIVE : _faultSigActiveKey,
                "Fault signal active",
                details.c_str()
            );
        } else {
            IncidentMgr::shared()->clear(
                _deviceID,
                "FAULT_SIG_ACTIVE",
                _faultSigActiveKey.empty() ? KEY_FAULT_SIG_ACTIVE : _faultSigActiveKey
            );
        }

        _lastFaultActive = _faultActive;
        _hasLastFaultState = true;
    }

    gettimeofday(&_lastQueryTime, NULL);

    return true;
}

/**
 * @brief Handle device action requests.
 *
 * FAULT_SIG currently has no runtime actions. This function accepts/logs the
 * request only when enabled so the plugin keeps the base driver shape, but it
 * intentionally performs no hardware action.
 *
 * @param cmd Device action command string.
 * @return true if the device is enabled and the action was accepted as a no-op.
 */
bool FAULT_SIG_Device::deviceAction(std::string cmd)
{
    bool result = false;

    if(_isEnabled) {
        LOGT_DEBUG("FAULT_SIG_Device devID \"%s\" DEVICE_ACTION : \"%s\" ",
                   _deviceID.c_str(),
                   cmd.c_str());
        result = true;
    }

    return result;
}
