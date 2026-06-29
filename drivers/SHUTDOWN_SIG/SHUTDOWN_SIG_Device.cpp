//
//  SHUTDOWN_SIG_Device.cpp
//  pIoTServer
//
//  Runtime SHUTDOWN_SIG pIoTServer plugin.
//
//  This plugin monitors a GPIO shutdown-request signal through the shared GPIO
//  helper class. It exposes the configured BOOL key, usually BATLOW_ACTIVE,
//  and requests a controlled pIoTServer shutdown after the signal remains
//  active for the configured debounce/assert delay.
//
//  Current schema example:
//
//           {
//                 "deviceID": "BATLOW_SIG",
//                 "device_type": "SHUTDOWN_SIG",
//                 "enable": true,
//                 "interval": 1,
//                 "pins": [
//                     {
//                         "data_type": "BOOL",
//                         "key": "BATLOW_ACTIVE",
//                         "other.props": {
//                             "active_low": true,
//                             "assert_delay_sec": 5,
//                             "debounce_ms": 250,
//                             "gpio_pin": 23,
//                             "gpio_pull": "up"
//                         },
//                         "read_only": true,
//                         "title": "Battery Low Shutdown Signal",
//                         "tracking": "track.changes"
//                     }
//                 ],
//                 "title": "Battery Low Shutdown"
//          }
//

#include "SHUTDOWN_SIG_Device.hpp"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <limits>

#include "IncidentMgr.hpp"
#include "LogMgr.hpp"
#include "pIoTServerMgr.hpp"

bool SHUTDOWN_SIG_Device::_instanceExists = false;


/**
 * @brief Construct the SHUTDOWN_SIG plugin instance.
 *
 * The constructor records the pIoTServer device identity and enforces the
 * singleton rule. It does not open GPIO hardware. GPIO setup is deferred to
 * start() after initWithSchema() has registered the configured value key.
 *
 * @param devID pIoTServer device ID.
 * @param driverName pIoTServer driver/plugin name.
 */
SHUTDOWN_SIG_Device::SHUTDOWN_SIG_Device(std::string devID, std::string driverName)
{
    setDeviceID(devID, driverName);

    _deviceState = DEVICE_STATE_UNKNOWN;
    _isSetup = false;

    if(_instanceExists) {
        LOGT_ERROR("SHUTDOWN_SIG_Device only supports one instance");
        _deviceState = DEVICE_STATE_ERROR;
        return;
    }

    _instanceExists = true;
}

/**
 * @brief Destroy the SHUTDOWN_SIG plugin instance.
 *
 * Releases GPIO resources and clears the singleton guard.
 */
SHUTDOWN_SIG_Device::~SHUTDOWN_SIG_Device()
{
    stop();
    _instanceExists = false;
}

/**
 * @brief Return the SHUTDOWN_SIG plugin version.
 *
 * Uses the build-provided SHUTDOWN_SIG_DRIVER_VERSION macro when present.
 * Falls back to the driver name for older/simple builds.
 *
 * @param version Receives version string.
 * @return true.
 */
bool SHUTDOWN_SIG_Device::getVersion(std::string &version)
{
#ifdef SHUTDOWN_SIG_DRIVER_VERSION
    version = SHUTDOWN_SIG_DRIVER_VERSION;
#else
    version = "SHUTDOWN_SIG";
#endif
    return true;
}

/**
 * @brief Initialize the driver from pIoTServer schema.
 *
 * Discovers the configured BOOL key and records the shortest queryDelay
 * supplied by the schema.
 *
 * The GPIO behavior is configured from the first supported schema entry:
 *
 * @code{.json}
 * "other.props": {
 *     "active_low": true,
 *     "gpio_pin": 23,
 *     "gpio_pull": "up",
 *     "debounce_ms": 250,
 *     "assert_delay_sec": 5
 * }
 * @endcode
 *
 * @param deviceSchema pIoTServer device schema entries for this plugin.
 * @return true if a shutdown signal key was found and accepted.
 */
bool SHUTDOWN_SIG_Device::initWithSchema(deviceSchemaMap_t deviceSchema)
{
    uint64_t delay = UINT64_MAX;

    _shutdownSigActiveKey.clear();

    _gpioLine = default_gpioLine;
    _activeLow = default_activeLow;
    _gpioPull = "up";
    _debounceMs = default_debounceMs;
    _assertDelaySec = default_assertDelaySec;
    _queryDelay = default_queryDelay;

    _shutdownActive = false;
    _lastShutdownActive = false;
    _hasLastShutdownState = false;
    _assertTimingActive = false;
    _assertStartTime = {};
    _shutdownRequestSent = false;

    _isSetup = false;

    for(const auto& [key, entry] : deviceSchema) {

        /*
         * SHUTDOWN_SIG owns one read-only BOOL input key.
         *
         * Do not require the key to literally be SHUTDOWN_SIG_ACTIVE because
         * the farm config uses BATLOW_ACTIVE.
         */
        if(_shutdownSigActiveKey.empty()) {
            _shutdownSigActiveKey = key;

            if(entry.queryDelay < delay) {
                delay = entry.queryDelay;
            }

            if(entry.otherProps.contains("active_low")) {
                try {
                    _activeLow = entry.otherProps.at("active_low").get<bool>();
                }
                catch(...) {
                    LOGT_ERROR("SHUTDOWN_SIG_Device devID \"%s\" invalid active_low value",
                               _deviceID.c_str());
                    _deviceState = DEVICE_STATE_ERROR;
                    return false;
                }
            }

            if(entry.otherProps.contains("gpio_pin")) {
                try {
                    _gpioLine = entry.otherProps.at("gpio_pin").get<int>();
                }
                catch(...) {
                    LOGT_ERROR("SHUTDOWN_SIG_Device devID \"%s\" invalid gpio_pin value",
                               _deviceID.c_str());
                    _deviceState = DEVICE_STATE_ERROR;
                    return false;
                }
            }

            if(entry.otherProps.contains("gpio_pull")) {
                try {
                    _gpioPull = entry.otherProps.at("gpio_pull").get<std::string>();
                }
                catch(...) {
                    LOGT_ERROR("SHUTDOWN_SIG_Device devID \"%s\" invalid gpio_pull value",
                               _deviceID.c_str());
                    _deviceState = DEVICE_STATE_ERROR;
                    return false;
                }

                if(_gpioPull != "up" && _gpioPull != "down" && _gpioPull != "none") {
                    LOGT_ERROR("SHUTDOWN_SIG_Device devID \"%s\" unsupported gpio_pull \"%s\"",
                               _deviceID.c_str(),
                               _gpioPull.c_str());
                    _deviceState = DEVICE_STATE_ERROR;
                    return false;
                }
            }

            if(entry.otherProps.contains("debounce_ms")) {
                try {
                    _debounceMs = entry.otherProps.at("debounce_ms").get<uint64_t>();
                }
                catch(...) {
                    LOGT_ERROR("SHUTDOWN_SIG_Device devID \"%s\" invalid debounce_ms value",
                               _deviceID.c_str());
                    _deviceState = DEVICE_STATE_ERROR;
                    return false;
                }
            }

            if(entry.otherProps.contains("assert_delay_sec")) {
                try {
                    _assertDelaySec = entry.otherProps.at("assert_delay_sec").get<uint64_t>();
                }
                catch(...) {
                    LOGT_ERROR("SHUTDOWN_SIG_Device devID \"%s\" invalid assert_delay_sec value",
                               _deviceID.c_str());
                    _deviceState = DEVICE_STATE_ERROR;
                    return false;
                }
            }

            _isSetup = true;
        }
        else {
            LOGT_ERROR("SHUTDOWN_SIG_Device ignored unsupported extra key \"%s\"",
                       key.c_str());
        }
    }

    _queryDelay = delay != UINT64_MAX ? delay : default_queryDelay;
    _deviceState = DEVICE_STATE_DISCONNECTED;

    // LOGT_DEBUG("SHUTDOWN_SIG_Device devID \"%s\" setup=%s shutdownSigActiveKey=\"%s\" gpio=%d activeLow=%s gpioPull=%s debounceMs=%llu assertDelaySec=%llu queryDelay=%llu",
    //            _deviceID.c_str(),
    //            _isSetup ? "true" : "false",
    //            _shutdownSigActiveKey.c_str(),
    //            _gpioLine,
    //            _activeLow ? "true" : "false",
    //            _gpioPull.c_str(),
    //            static_cast<unsigned long long>(_debounceMs),
    //            static_cast<unsigned long long>(_assertDelaySec),
    //            static_cast<unsigned long long>(_queryDelay));

    return _isSetup;
}

/**
 * @brief Start the SHUTDOWN_SIG plugin.
 *
 * Opens the configured GPIO line as an input through the shared GPIO helper.
 * This method does not request shutdown. getValues() performs the actual
 * polling and active-state timing.
 *
 * @return true if GPIO setup succeeds.
 */
bool SHUTDOWN_SIG_Device::start()
{
    if(!_isSetup) {
        LOGT_ERROR("SHUTDOWN_SIG_Device devID \"%s\" start failed: device is not setup",
                   _deviceID.c_str());
        _deviceState = DEVICE_STATE_ERROR;
        return false;
    }

    int error = 0;

    GPIO::gpio_pin_t shutdownPin = {};
    shutdownPin.lineNo = static_cast<unsigned int>(_gpioLine);
    shutdownPin.direction = GPIO::GPIO_DIRECTION_INPUT;
    shutdownPin.flags = 0;

    if(_gpioPull == "up") {
        shutdownPin.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
    }
    else if(_gpioPull == "down") {
        shutdownPin.flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
    }
    else if(_gpioPull == "none") {
        shutdownPin.flags |= GPIO_V2_LINE_FLAG_BIAS_DISABLED;
    }

    // LOGT_ERROR("SHUTDOWN_SIG_Device devID \"%s\" requesting GPIO line %d flags=0x%X gpio_pull=%s active_low=%s",
    //            _deviceID.c_str(),
    //            _gpioLine,
    //            shutdownPin.flags,
    //            _gpioPull.c_str(),
    //            _activeLow ? "true" : "false");

    if(!_gpio.begin({ shutdownPin }, error)) {
        LOGT_ERROR("SHUTDOWN_SIG_Device devID \"%s\" failed to open GPIO line %d: %s",
                   _deviceID.c_str(),
                   _gpioLine,
                   strerror(error));

        _deviceState = DEVICE_STATE_ERROR;
        return false;
    }

    gettimeofday(&_lastQueryTime, NULL);

    _shutdownActive = false;
    _lastShutdownActive = false;
    _hasLastShutdownState = false;
    _assertTimingActive = false;
    _assertStartTime = {};
    _shutdownRequestSent = false;

    _deviceState = DEVICE_STATE_CONNECTED;

    LOGT_INFO("SHUTDOWN_SIG_Device devID \"%s\" started gpio=%d active_low=%s gpio_pull=%s debounceMs=%llu assertDelaySec=%llu queryDelay=%llu",
              _deviceID.c_str(),
              _gpioLine,
              _activeLow ? "true" : "false",
              _gpioPull.c_str(),
              static_cast<unsigned long long>(_debounceMs),
              static_cast<unsigned long long>(_assertDelaySec),
              static_cast<unsigned long long>(_queryDelay));

    return true;
}

/**
 * @brief Stop the SHUTDOWN_SIG plugin.
 *
 * Releases GPIO resources. If the device is already in DEVICE_STATE_ERROR,
 * the error state is preserved for diagnostics.
 */
void SHUTDOWN_SIG_Device::stop()
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
bool SHUTDOWN_SIG_Device::setEnabled(bool enable)
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
 * @brief Return whether the SHUTDOWN_SIG GPIO input is ready.
 *
 * @return true if the device is enabled, connected, and GPIO is available.
 */
bool SHUTDOWN_SIG_Device::isConnected()
{
    return (_isEnabled && _deviceState == DEVICE_STATE_CONNECTED && _gpio.isAvailable());
}

/**
 * @brief Read and publish the BAT_LOW shutdown signal state.
 *
 * Reads the configured GPIO line through the shared GPIO helper.
 *
 * The shared GPIO helper returns the raw electrical GPIO level:
 *
 *   true  = GPIO high
 *   false = GPIO low
 *
 * This driver converts the raw GPIO level into the logical BAT_LOW state
 * using the configured polarity:
 *
 *   active_low=true:
 *       GPIO high = BAT_LOW clear
 *       GPIO low  = BAT_LOW asserted
 *
 * For the current BATLOW hardware:
 *
 *   normal     -> GPIO23 high -> BAT_LOW clear
 *   off button -> GPIO23 low  -> BAT_LOW asserted
 *
 * Normal idle startup is intentionally quiet.
 *
 * Incident behavior:
 *
 *   - GPIO read failure raises SHUTDOWN_SIG_READ_FAILED.
 *   - Missing GPIO line in the read result raises SHUTDOWN_SIG_READ_FAILED.
 *   - Successful read clears SHUTDOWN_SIG_READ_FAILED.
 *   - BAT_LOW clear -> asserted raises SHUTDOWN_SIG_ACTIVE.
 *   - BAT_LOW asserted -> clear clears SHUTDOWN_SIG_ACTIVE only if shutdown
 *     has not already been requested.
 *
 * Shutdown behavior:
 *
 *   - If BAT_LOW remains asserted for debounce_ms plus assert_delay_sec,
 *     processShutdownState() raises SHUTDOWN_REQUESTED.
 *   - After SHUTDOWN_REQUESTED, later clear transitions are quiet.
 *
 * @param results Receives pIoTServer key/value output.
 * @return true if the GPIO line was read successfully.
 */
bool SHUTDOWN_SIG_Device::getValues(keyValueMap_t &results)
{
    if(!isConnected()) {
        return false;
    }

    GPIO::gpioStates_t states;

    if(!_gpio.get(states)) {
        LOGT_ERROR("SHUTDOWN_SIG_Device devID \"%s\" failed reading GPIO line %d",
                   _deviceID.c_str(),
                   _gpioLine);

        _deviceState = DEVICE_STATE_ERROR;

        std::string details =
            "gpio=" + std::to_string(_gpioLine) +
            " error=gpio_read_failed";

        IncidentMgr::shared()->raise(
            _deviceID,
            IncidentMgr::Severity::Error,
            "SHUTDOWN_SIG_READ_FAILED",
            _shutdownSigActiveKey.empty() ? KEY_SHUTDOWN_SIG_ACTIVE : _shutdownSigActiveKey,
            "Shutdown signal GPIO read failed",
            details.c_str()
        );

        return false;
    }

    bool gpioHigh = false;
    bool found = false;

    for(const auto &state : states) {
        if(state.first == _gpioLine) {
            /*
             * GPIO::get() returns the raw electrical GPIO level.
             *
             * true  = high
             * false = low
             */
            gpioHigh = state.second;
            found = true;
            break;
        }
    }

    if(!found) {
        LOGT_ERROR("SHUTDOWN_SIG_Device devID \"%s\" GPIO line %d not found in read result",
                   _deviceID.c_str(),
                   _gpioLine);

        _deviceState = DEVICE_STATE_ERROR;

        std::string details =
            "gpio=" + std::to_string(_gpioLine) +
            " error=gpio_line_missing";

        IncidentMgr::shared()->raise(
            _deviceID,
            IncidentMgr::Severity::Error,
            "SHUTDOWN_SIG_READ_FAILED",
            _shutdownSigActiveKey.empty() ? KEY_SHUTDOWN_SIG_ACTIVE : _shutdownSigActiveKey,
            "Shutdown signal GPIO line missing from read result",
            details.c_str()
        );

        return false;
    }

    IncidentMgr::shared()->clear(
        _deviceID,
        "SHUTDOWN_SIG_READ_FAILED",
        _shutdownSigActiveKey.empty() ? KEY_SHUTDOWN_SIG_ACTIVE : _shutdownSigActiveKey
    );

    /*
     * Convert raw GPIO level to BAT_LOW logical state.
     *
     * With active_low=true:
     *
     *   gpioHigh=true  -> BAT_LOW clear
     *   gpioHigh=false -> BAT_LOW asserted
     */
    bool batLowAsserted = _activeLow ? !gpioHigh : gpioHigh;

    _shutdownActive = batLowAsserted;

    if(!_shutdownSigActiveKey.empty()) {
        results[_shutdownSigActiveKey] = batLowAsserted ? "1" : "0";
    }

    bool firstRead = !_hasLastShutdownState;
    bool stateChanged = _hasLastShutdownState && (_shutdownActive != _lastShutdownActive);

    /*
     * First read:
     *
     * - Record baseline.
     * - If BAT_LOW is clear, do not log. Normal startup should be quiet.
     * - If BAT_LOW is asserted, fall through and raise/log the condition.
     */
    if(firstRead) {
        _lastShutdownActive = _shutdownActive;
        _hasLastShutdownState = true;

        if(!batLowAsserted) {
            processShutdownState(batLowAsserted);
            gettimeofday(&_lastQueryTime, NULL);
            return true;
        }
    }

    /*
     * No change after first read:
     *
     * - Keep reporting the value.
     * - Keep feeding the shutdown timing logic.
     * - Do not log.
     */
    if(!firstRead && !stateChanged) {
        processShutdownState(batLowAsserted);
        gettimeofday(&_lastQueryTime, NULL);
        return true;
    }

    /*
     * If shutdown has already been confirmed/requested, ignore later clear
     * transitions. The signal is one-shot and the system is expected to be
     * heading down.
     */
    if(_shutdownRequestSent && !batLowAsserted) {
        _lastShutdownActive = _shutdownActive;
        _hasLastShutdownState = true;

        processShutdownState(batLowAsserted);
        gettimeofday(&_lastQueryTime, NULL);
        return true;
    }

    std::string details =
        "gpio=" + std::to_string(_gpioLine) +
        " gpio_level=" + std::string(gpioHigh ? "high" : "low") +
        " polarity=" + std::string(_activeLow ? "low_asserted" : "high_asserted") +
        " bat_low=" + std::string(batLowAsserted ? "asserted" : "clear") +
        " gpio_pull=" + _gpioPull;

    // if(batLowAsserted) {
    //     LOGT_ERROR("SHUTDOWN_SIG_Device devID \"%s\" BAT_LOW asserted gpio=%d gpio_level=%s key=%s polarity=%s gpio_pull=%s",
    //                _deviceID.c_str(),
    //                _gpioLine,
    //                gpioHigh ? "high" : "low",
    //                _shutdownSigActiveKey.c_str(),
    //                _activeLow ? "low_asserted" : "high_asserted",
    //                _gpioPull.c_str());

    //     IncidentMgr::shared()->raise(
    //         _deviceID,
    //         IncidentMgr::Severity::Error,
    //         "SHUTDOWN_SIG_ACTIVE",
    //         _shutdownSigActiveKey.empty() ? KEY_SHUTDOWN_SIG_ACTIVE : _shutdownSigActiveKey,
    //         "BAT_LOW shutdown signal asserted",
    //         details.c_str()
    //     );
    // }
    // else {
    //     LOGT_INFO("SHUTDOWN_SIG_Device devID \"%s\" BAT_LOW clear gpio=%d gpio_level=%s key=%s polarity=%s gpio_pull=%s",
    //               _deviceID.c_str(),
    //               _gpioLine,
    //               gpioHigh ? "high" : "low",
    //               _shutdownSigActiveKey.c_str(),
    //               _activeLow ? "low_asserted" : "high_asserted",
    //               _gpioPull.c_str());

    //     IncidentMgr::shared()->clear(
    //         _deviceID,
    //         "SHUTDOWN_SIG_ACTIVE",
    //         _shutdownSigActiveKey.empty() ? KEY_SHUTDOWN_SIG_ACTIVE : _shutdownSigActiveKey,
    //         "BAT_LOW shutdown signal clear",
    //         details.c_str()
    //     );
    // }

    _lastShutdownActive = _shutdownActive;
    _hasLastShutdownState = true;

    processShutdownState(batLowAsserted);

    gettimeofday(&_lastQueryTime, NULL);

    return true;
}

/**
 * @brief Apply debounce/assert delay and request shutdown when BAT_LOW is asserted.
 *
 * This function does not block. It uses the periodic getValues() polling calls
 * to decide whether BAT_LOW has remained asserted long enough.
 *
 * Once the shutdown request has been sent, this function does nothing.
 *
 * @param batLowAsserted true if BAT_LOW is asserted, false if clear.
 */
void SHUTDOWN_SIG_Device::processShutdownState(bool batLowAsserted)
{
    struct timeval now = {};
    gettimeofday(&now, NULL);

    uint64_t requiredMs = _debounceMs + (_assertDelaySec * 1000ULL);

    /*
     * If shutdown has already been requested, do not keep logging or
     * re-requesting shutdown while BAT_LOW remains asserted.
     */
    if(_shutdownRequestSent) {
        return;
    }

    /*
     * BAT_LOW is clear.
     *
     * If we were timing a possible assertion, cancel that pending assertion.
     * Do not log this as an incident; it was never confirmed.
     */
    if(!batLowAsserted) {
        _assertTimingActive = false;
        _assertStartTime = {};
        return;
    }

    /*
     * BAT_LOW first observed asserted. Start timing, but do not log an error,
     * raise an incident, or request shutdown yet.
     */
    if(!_assertTimingActive) {
        _assertTimingActive = true;
        _assertStartTime = now;
        return;
    }

    /*
     * BAT_LOW is still asserted. Check elapsed time.
     */
    uint64_t assertedMs =
        static_cast<uint64_t>(now.tv_sec - _assertStartTime.tv_sec) * 1000ULL;

    if(now.tv_usec >= _assertStartTime.tv_usec) {
        assertedMs += static_cast<uint64_t>(now.tv_usec - _assertStartTime.tv_usec) / 1000ULL;
    }
    else {
        assertedMs -= 1000ULL;
        assertedMs += static_cast<uint64_t>((1000000L + now.tv_usec) - _assertStartTime.tv_usec) / 1000ULL;
    }

    if(assertedMs < requiredMs) {
        return;
    }

    /*
     * Confirmed BAT_LOW.
     *
     * This is the first point where BAT_LOW is treated as a real shutdown
     * event.
     */
    _shutdownRequestSent = true;

    string key = _shutdownSigActiveKey.empty()
        ? KEY_SHUTDOWN_SIG_ACTIVE
        : _shutdownSigActiveKey;

    string details =
        "gpio=" + std::to_string(_gpioLine) +
        " key=" + key +
        " polarity=" + (_activeLow ? "low_asserted" : "high_asserted") +
        " gpio_pull=" + _gpioPull +
        " asserted_ms=" + std::to_string(assertedMs) +
        " required_ms=" + std::to_string(requiredMs);

    LOGT_DEBUG("SHUTDOWN_SIG_Device BAT_LOW detected");

    IncidentMgr::shared()->notice(
        _deviceID,
        "SHUTDOWN_REQUESTED",
        key,
        "Battery Low - requesting shutdown",
        nullptr
    );

    pIoTServerMgr::shared()->requestSystemShutdown("BAT_LOW asserted: " + key);
}

/**
 * @brief Return elapsed milliseconds between two timeval values.
 *
 * @param start Start time.
 * @param end End time.
 * @return elapsed milliseconds.
 */
uint64_t SHUTDOWN_SIG_Device::elapsedMs(const timeval& start, const timeval& end) const
{
    uint64_t startMs =
        (static_cast<uint64_t>(start.tv_sec) * 1000ULL) +
        (static_cast<uint64_t>(start.tv_usec) / 1000ULL);

    uint64_t endMs =
        (static_cast<uint64_t>(end.tv_sec) * 1000ULL) +
        (static_cast<uint64_t>(end.tv_usec) / 1000ULL);

    if(endMs < startMs) {
        return 0;
    }

    return endMs - startMs;
}

/**
 * @brief Handle device action requests.
 *
 * SHUTDOWN_SIG currently has no runtime actions. This function accepts/logs the
 * request only when enabled so the plugin keeps the base driver shape, but it
 * intentionally performs no hardware action.
 *
 * @param cmd Device action command string.
 * @return true if the device is enabled and the action was accepted as a no-op.
 */
bool SHUTDOWN_SIG_Device::deviceAction(std::string cmd)
{
    bool result = false;

    if(_isEnabled) {
        LOGT_DEBUG("SHUTDOWN_SIG_Device devID \"%s\" DEVICE_ACTION : \"%s\" ",
                   _deviceID.c_str(),
                   cmd.c_str());
        result = true;
    }

    return result;
}
