//
//  FAULT_SIG_Device.hpp
//  pIoTServer
//
//  Runtime FAULT_SIG pIoTServer plugin.
//
//  This driver monitors the farm hardware fault-signal GPIO line and exposes
//  it as a read-only BOOL value.
//
//  Hardware policy:
//
//    - FAULT_SIG is a singleton device.
//    - GPIO22 is used by default.
//    - The signal is active-low by default.
//    - GPIO high means no fault when active_low=true.
//    - GPIO low means fault active when active_low=true.
//
//  Fault qualification:
//
//    - Raw GPIO state is read every interval.
//    - A fault must remain active for assert_delay_sec before it is reported.
//    - A reported fault must remain inactive for clear_delay_sec before it is
//      cleared.
//    - This prevents short VALVEMASTER retry pulses from becoming incidents.
//

#ifndef FAULT_SIG_Device_hpp
#define FAULT_SIG_Device_hpp

#include <sys/time.h>
#include <ctime>
#include <string>

#include "pIoTServerDevice.hpp"
#include "GPIO.hpp"

/**
 * @brief pIoTServer device plugin for the hardware fault-signal GPIO line.
 */
class FAULT_SIG_Device : public pIoTServerDevice {

public:

    /**
     * @brief Supported pIoTServer value key.
     */
    static constexpr const char* KEY_FAULT_SIG_ACTIVE = "FAULT_SIG_ACTIVE";

    /**
     * @brief Default polling delay in seconds.
     */
    static const uint64_t default_queryDelay = 1;

    /**
     * @brief Default Raspberry Pi GPIO line number.
     */
    static const int default_gpioLine = 22;

    /**
     * @brief Default fault-signal polarity.
     */
    static const bool default_activeLow = true;

    /**
     * @brief Default active qualification delay in seconds.
     *
     * Default remains conservative: one full polling interval. Production
     * farm configs can raise this to suppress normal VALVEMASTER retry pulses.
     */
    static const uint64_t default_assertDelaySec = 1;

    /**
     * @brief Default inactive qualification delay in seconds.
     */
    static const uint64_t default_clearDelaySec = 1;

    /**
     * @brief Construct FAULT_SIG plugin instance.
     *
     * @param devID pIoTServer device ID.
     * @param driverName pIoTServer driver/plugin name.
     */
    FAULT_SIG_Device(std::string devID, std::string driverName);

    /**
     * @brief Destroy FAULT_SIG plugin instance.
     */
    ~FAULT_SIG_Device();

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
     * @brief Read current fault-signal value.
     *
     * @param results Receives key/value output.
     * @return true if GPIO read succeeds.
     */
    bool getValues(keyValueMap_t &results);

    /**
     * @brief Handle device action request.
     *
     * FAULT_SIG currently has no hardware actions.
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
     * @brief Configured pIoTServer key for fault-active state.
     */
    std::string _faultSigActiveKey;

    /**
     * @brief Shared GPIO helper.
     */
    GPIO _gpio;

    /**
     * @brief GPIO line number being monitored.
     */
    int _gpioLine = default_gpioLine;

    /**
     * @brief True when electrical low means fault active.
     */
    bool _activeLow = default_activeLow;

    /**
     * @brief Active qualification delay in seconds.
     */
    uint64_t _assertDelaySec = default_assertDelaySec;

    /**
     * @brief Inactive qualification delay in seconds.
     */
    uint64_t _clearDelaySec = default_clearDelaySec;

    /**
     * @brief Current raw logical fault-active state from GPIO.
     */
    bool _rawFaultActive = false;

    /**
     * @brief Last raw logical fault-active state from GPIO.
     */
    bool _lastRawFaultActive = false;

    /**
     * @brief True after first successful raw fault-state read.
     */
    bool _hasLastRawFaultState = false;

    /**
     * @brief Time when the raw logical fault state last changed.
     */
    time_t _rawFaultChangedAt = 0;

    /**
     * @brief Current debounced/reported fault-active state.
     */
    bool _faultActive = false;

    /**
     * @brief Last debounced/reported fault-active state.
     */
    bool _lastFaultActive = false;

    /**
     * @brief True after first debounced fault-state publication.
     */
    bool _hasLastFaultState = false;
};

#endif /* FAULT_SIG_Device_hpp */
