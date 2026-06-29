//
//  SHUTDOWN_SIG_Device.hpp
//  pIoTServer
//
//  Runtime SHUTDOWN_SIG pIoTServer plugin.
//
//  This driver monitors a GPIO shutdown-request signal and exposes it as a
//  read-only BOOL value.
//
//  Intended farm use:
//
//    - BATLOW from the power supply/opto output goes to Raspberry Pi GPIO23.
//    - The signal is active-low by default.
//    - GPIO high means no shutdown request when active_low=true.
//    - GPIO low means shutdown request active when active_low=true.
//    - Once the signal remains active long enough, this driver requests a
//      controlled pIoTServer system shutdown.
//    - The driver is one-shot: after requesting shutdown, it will not request
//      it again.
//

#ifndef SHUTDOWN_SIG_Device_hpp
#define SHUTDOWN_SIG_Device_hpp

#include <sys/time.h>
#include <string>

#include "pIoTServerDevice.hpp"
#include "GPIO.hpp"

/**
 * @brief pIoTServer device plugin for a GPIO shutdown-request signal.
 */
class SHUTDOWN_SIG_Device : public pIoTServerDevice {

public:

    /**
     * @brief Default pIoTServer value key.
     *
     * The actual key may be overridden by schema. For example, farm.props.json
     * may use BATLOW_ACTIVE.
     */
    static constexpr const char* KEY_SHUTDOWN_SIG_ACTIVE = "SHUTDOWN_SIG_ACTIVE";

    /**
     * @brief Default polling delay in seconds.
     */
    static const uint64_t default_queryDelay = 1;

    /**
     * @brief Default Raspberry Pi GPIO line number.
     */
    static const int default_gpioLine = 23;

    /**
     * @brief Default shutdown-signal polarity.
     */
    static const bool default_activeLow = true;

    /**
     * @brief Default debounce time in milliseconds.
     */
    static const uint64_t default_debounceMs = 250;

    /**
     * @brief Default active assertion delay in seconds.
     *
     * The GPIO must remain active for debounce_ms plus assert_delay_sec before
     * the controlled shutdown request is fired.
     */
    static const uint64_t default_assertDelaySec = 5;

    /**
     * @brief Construct SHUTDOWN_SIG plugin instance.
     *
     * @param devID pIoTServer device ID.
     * @param driverName pIoTServer driver/plugin name.
     */
    SHUTDOWN_SIG_Device(std::string devID, std::string driverName);

    /**
     * @brief Destroy SHUTDOWN_SIG plugin instance.
     */
    ~SHUTDOWN_SIG_Device();

    /**
     * @brief Return plugin version string.
     *
     * @param version Receives version string.
     * @return true.
     */
    bool getVersion(std::string &version);

    /**
     * @brief Initialize driver from pIoTServer schema.
     *
     * @param deviceSchema Schema entries owned by this device.
     * @return true if setup succeeds.
     */
    bool initWithSchema(deviceSchemaMap_t deviceSchema);

    /**
     * @brief Start GPIO monitoring.
     *
     * @return true if GPIO setup succeeds.
     */
    bool start();

    /**
     * @brief Stop GPIO monitoring and release resources.
     */
    void stop();

    /**
     * @brief Return whether GPIO input is available.
     *
     * @return true if enabled, connected, and GPIO is open.
     */
    bool isConnected();

    /**
     * @brief Enable or disable this device.
     *
     * @param enable true to enable, false to disable.
     * @return true if requested state was reached.
     */
    bool setEnabled(bool enable);

    /**
     * @brief Read current shutdown-signal value.
     *
     * @param results Receives key/value output.
     * @return true if GPIO read succeeds.
     */
    bool getValues(keyValueMap_t &results);

    /**
     * @brief Handle device action request.
     *
     * SHUTDOWN_SIG currently has no hardware actions.
     *
     * @param cmd Device action command.
     * @return true if accepted.
     */
    bool deviceAction(std::string cmd);

private:

    /**
     * @brief Singleton guard.
     */
    static bool _instanceExists;

    /**
     * @brief True after schema setup succeeds.
     */
    bool _isSetup = false;

    /**
     * @brief Last successful GPIO query time.
     */
    timeval _lastQueryTime = {};

    /**
     * @brief Polling delay in seconds.
     */
    uint64_t _queryDelay = default_queryDelay;

    /**
     * @brief Configured pIoTServer key for shutdown-active state.
     */
    std::string _shutdownSigActiveKey;

    /**
     * @brief Shared GPIO helper.
     */
    GPIO _gpio;

    /**
     * @brief GPIO line number being monitored.
     */
    int _gpioLine = default_gpioLine;

    /**
     * @brief True when electrical low means shutdown active.
     */
    bool _activeLow = default_activeLow;

    /**
     * @brief Optional GPIO pull setting from schema.
     *
     * Supported values are currently "up", "down", and "none".
     */
    std::string _gpioPull = "up";

    /**
     * @brief Debounce time in milliseconds.
     */
    uint64_t _debounceMs = default_debounceMs;

    /**
     * @brief Active assertion delay in seconds.
     */
    uint64_t _assertDelaySec = default_assertDelaySec;

    /**
     * @brief Current logical shutdown-active state.
     */
    bool _shutdownActive = false;

    /**
     * @brief Last reported logical shutdown-active state.
     */
    bool _lastShutdownActive = false;

    /**
     * @brief True after first successful shutdown-state read.
     */
    bool _hasLastShutdownState = false;

    /**
     * @brief True while timing a continuous active assertion.
     */
    bool _assertTimingActive = false;

    /**
     * @brief Time when the current active assertion first started.
     */
    timeval _assertStartTime = {};

    /**
     * @brief True after this driver has requested controlled shutdown.
     *
     * One-shot latch.
     */
    bool _shutdownRequestSent = false;

    /**
     * @brief Apply debounce/assert delay and request shutdown when confirmed.
     *
     * @param rawHigh Raw GPIO electrical level.
     */
    void processShutdownState(bool rawHigh);

    /**
     * @brief Return elapsed milliseconds between two timeval values.
     *
     * @param start Start time.
     * @param end End time.
     * @return elapsed milliseconds.
     */
    uint64_t elapsedMs(const timeval& start, const timeval& end) const;
};

#endif /* SHUTDOWN_SIG_Device_hpp */
