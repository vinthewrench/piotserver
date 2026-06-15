//
//  CPUTEMP_Device.cpp
//  piServer
//
//  Created by vinnie on 12/28/24.
//

#include "CPUTEMP_Device.hpp"
#include "TimeStamp.hpp"
#include "LogMgr.hpp"
#include "PropValKeys.hpp"
#include "IncidentMgr.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <sys/wait.h>

/**
 * @brief Fixed device ID used by the CPU temperature pseudo-device.
 */
static constexpr const char* CPUTEMP_DEVICE_ID = "CPUTEMP";

/**
 * @brief CPU temperature warning threshold in degrees Celsius.
 *
 * A CPU_TEMP_HIGH incident is raised at or above this temperature.
 */
static constexpr double CPU_TEMP_HIGH_C = 80.0;

/**
 * @brief CPU temperature warning clear threshold in degrees Celsius.
 *
 * Hysteresis keeps the warning from flapping around the trip point.
 */
static constexpr double CPU_TEMP_HIGH_CLEAR_C = 75.0;

/**
 * @brief CPU temperature critical threshold in degrees Celsius.
 *
 * A CPU_TEMP_CRITICAL incident is raised at or above this temperature.
 */
static constexpr double CPU_TEMP_CRITICAL_C = 85.0;

/**
 * @brief CPU temperature critical clear threshold in degrees Celsius.
 *
 * Hysteresis keeps the critical incident from flapping around the trip point.
 */
static constexpr double CPU_TEMP_CRITICAL_CLEAR_C = 80.0;

/**
 * @brief Raspberry Pi vcgencmd get_throttled active under-voltage bit.
 */
static constexpr uint32_t RPI_THROTTLE_UNDERVOLT_NOW = 1u << 0;

/**
 * @brief Raspberry Pi vcgencmd get_throttled active frequency-capped bit.
 */
static constexpr uint32_t RPI_THROTTLE_FREQ_CAPPED_NOW = 1u << 1;

/**
 * @brief Raspberry Pi vcgencmd get_throttled active throttled bit.
 */
static constexpr uint32_t RPI_THROTTLE_THROTTLED_NOW = 1u << 2;

/**
 * @brief Raspberry Pi vcgencmd get_throttled active soft temperature limit bit.
 */
static constexpr uint32_t RPI_THROTTLE_SOFT_TEMP_NOW = 1u << 3;

/**
 * @brief Read Raspberry Pi throttling state using vcgencmd.
 *
 * vcgencmd get_throttled returns a hexadecimal bitfield:
 *
 * - bit 0:  under-voltage detected now
 * - bit 1:  ARM frequency capped now
 * - bit 2:  throttled now
 * - bit 3:  soft temperature limit active now
 * - bit 16: under-voltage has occurred
 * - bit 17: ARM frequency capping has occurred
 * - bit 18: throttling has occurred
 * - bit 19: soft temperature limit has occurred
 *
 * This helper only reads and returns the raw value. The caller decides which
 * bits are active incidents. Historical bits are useful diagnostics, but they
 * are not treated as current faults here.
 *
 * @param throttleStateOut Receives the raw vcgencmd throttled bitfield.
 * @return true if vcgencmd ran successfully and the value was parsed.
 * @return false if vcgencmd is unavailable, fails, or returns unexpected text.
 */
static bool getRaspberryPiThrottleState(uint32_t& throttleStateOut)
{
    throttleStateOut = 0;

    FILE* fp = popen("vcgencmd get_throttled 2>/dev/null", "r");
    if(fp == nullptr) {
        return false;
    }

    char buffer[64] = {0};

    if(fgets(buffer, sizeof(buffer), fp) == nullptr) {
        pclose(fp);
        return false;
    }

    int rc = pclose(fp);
    if(rc == -1 || !WIFEXITED(rc) || WEXITSTATUS(rc) != 0) {
        return false;
    }

    unsigned long value = 0;

    if(sscanf(buffer, "throttled=0x%lx", &value) != 1) {
        return false;
    }

    throttleStateOut = static_cast<uint32_t>(value);
    return true;
}

/**
 * @brief Construct the CPU temperature pseudo-device.
 */
CPUTEMP_Device::CPUTEMP_Device(){
    _state = INS_UNKNOWN;
    _lastQueryTime = {0,0};
    _isSetup = false;

    json j = {
        { PROP_DEVICE_MFG_URL, {"/sys/class/thermal/thermal_zone0/temp",
            "https://pip.raspberrypi.com/categories/685-whitepapers-app-notes/documents/RP-003608-WP/Cooling-a-Raspberry-Pi-device.pdf",
            "https://www.raspberrypi.com/products/active-cooler/"
        }},
        { PROP_DEVICE_MFG_PART, "Raspberry Pi Active Cooler"},
    };
    setProperties(j);
    _deviceState = DEVICE_STATE_UNKNOWN;
}

/**
 * @brief Destroy the CPU temperature pseudo-device.
 */
CPUTEMP_Device::~CPUTEMP_Device(){
    stop();
}

/**
 * @brief Initialize result keys and query interval from the device schema.
 *
 * Expected schema entries:
 *
 * - DEGREES_C: CPU temperature key
 * - INT: cooler/fan state key
 *
 * At least one supported key must be present for setup to succeed.
 *
 * @param deviceSchema Device schema map supplied by the server.
 * @return true if at least one supported schema entry was found.
 * @return false if no supported schema entries were found.
 */
bool CPUTEMP_Device::initWithSchema(deviceSchemaMap_t deviceSchema){

    uint64_t delay = UINT64_MAX;

    _isSetup = false;

    for(const auto& [key, entry] : deviceSchema) {
        if(entry.units == DEGREES_C ){
            _resultKey_temp = key;
            if(entry.queryDelay < delay)  delay = entry.queryDelay;
            _isSetup = true;
        }
        else if(entry.units == INT ){
            _resultKey_cooler = key;
            if(entry.queryDelay < delay)  delay = entry.queryDelay;
            _isSetup = true;
        }
    }

    _queryDelay = delay != UINT64_MAX? delay : default_queryDelay;
    _deviceState = DEVICE_STATE_DISCONNECTED;

    return _isSetup;
}

/**
 * @brief Check whether this device owns a given property key.
 *
 * @param key Property key to test.
 * @return true if the key is the CPU temperature or cooler state key.
 */
bool CPUTEMP_Device::hasKey(string key){
    return
    (key == _resultKey_temp)
    || (key == _resultKey_cooler);
}

/**
 * @brief Start the CPU temperature pseudo-device.
 *
 * This device does not open hardware directly. It reads Linux sysfs files during
 * polling, so start only marks the pseudo-device as connected.
 *
 * @return true if initialized and started.
 * @return false if called before schema setup.
 */
bool CPUTEMP_Device::start(){

    if(!_isSetup){
        LOGT_DEBUG("CPUTEMP_Device begin called before initWithKey");
        return  false;
    }

    LOGT_DEBUG("CPUTEMP_Device(%s) begin", _resultKey_temp.c_str());

    _lastQueryTime = {0,0};
    _state = INS_IDLE;
    _deviceState = DEVICE_STATE_CONNECTED;

    IncidentMgr::shared()->clear(
        CPUTEMP_DEVICE_ID,
        "DEVICE_IO_FAILED",
        _resultKey_temp,
        nullptr,
        "CPUTEMP started"
    );

    return true;
}

/**
 * @brief Stop the CPU temperature pseudo-device.
 */
void CPUTEMP_Device::stop(){

    LOGT_DEBUG("CPUTEMP_Device  stop");

    _state = INS_UNKNOWN;
    _lastQueryTime = {0,0};
    _deviceState = DEVICE_STATE_DISCONNECTED;
}

/**
 * @brief Report connection state.
 *
 * This is a pseudo-device backed by sysfs, so it is considered always connected.
 *
 * @return true always.
 */
bool CPUTEMP_Device::isConnected(){

    return true;
}

/**
 * @brief Return the fixed device ID.
 *
 * @param devID Receives "CPUTEMP".
 * @return true always.
 */
bool CPUTEMP_Device::getDeviceID(string  &devID){

    devID = CPUTEMP_DEVICE_ID;
    return true;
}

/**
 * @brief Poll CPU temperature, cooler state, and throttling status.
 *
 * Results:
 *
 * - CPU temperature is read from /sys/class/thermal/thermal_zone0/temp.
 * - Cooler state is read from /sys/class/thermal/cooling_device0/cur_state.
 * - Raspberry Pi throttling state is read from vcgencmd get_throttled when available.
 *
 * Incidents:
 *
 * - DEVICE_IO_FAILED for CPU temp or cooler read failures.
 * - CPU_TEMP_HIGH when CPU temperature reaches warning threshold.
 * - CPU_TEMP_CRITICAL when CPU temperature reaches critical threshold.
 * - CPU_THROTTLED when active Raspberry Pi throttle bits are set.
 *
 * @param results Receives updated key/value readings.
 * @return true if at least CPU temperature was read successfully.
 */
bool CPUTEMP_Device::getValues( keyValueMap_t &results){

    bool hasData = false;

    if(!isConnected()) {
        return false;
    }

    if(_state == INS_IDLE){

        bool shouldQuery = false;

        if(_lastQueryTime.tv_sec == 0 &&  _lastQueryTime.tv_usec == 0 ){
            shouldQuery = true;
        } else {

            timeval now, diff;
            gettimeofday(&now, NULL);
            timersub(&now, &_lastQueryTime, &diff);

            if(diff.tv_sec >=  _queryDelay  ) {
                shouldQuery = true;
            }
        }

        if(shouldQuery){

            double tempC = 0;
            uint8_t fanState = 0;

            if(getCPUTemp(tempC)){
                results[_resultKey_temp] = to_string(tempC);
                gettimeofday(&_lastQueryTime, NULL);

                IncidentMgr::shared()->clear(
                    CPUTEMP_DEVICE_ID,
                    "DEVICE_IO_FAILED",
                    _resultKey_temp,
                    nullptr,
                    "CPUTEMP temperature read succeeded"
                );

                if(tempC >= CPU_TEMP_CRITICAL_C) {
                    IncidentMgr::shared()->raise(
                        CPUTEMP_DEVICE_ID,
                        IncidentMgr::Severity::Critical,
                        "CPU_TEMP_CRITICAL",
                        _resultKey_temp,
                        nullptr,
                        "CPU temperature is critical"
                    );
                }
                else if(tempC < CPU_TEMP_CRITICAL_CLEAR_C) {
                    IncidentMgr::shared()->clear(
                        CPUTEMP_DEVICE_ID,
                        "CPU_TEMP_CRITICAL",
                        _resultKey_temp,
                        nullptr,
                        "CPU temperature dropped below critical threshold"
                    );
                }

                if(tempC >= CPU_TEMP_HIGH_C) {
                    IncidentMgr::shared()->raise(
                        CPUTEMP_DEVICE_ID,
                        IncidentMgr::Severity::Warning,
                        "CPU_TEMP_HIGH",
                        _resultKey_temp,
                        nullptr,
                        "CPU temperature is high"
                    );
                }
                else if(tempC < CPU_TEMP_HIGH_CLEAR_C) {
                    IncidentMgr::shared()->clear(
                        CPUTEMP_DEVICE_ID,
                        "CPU_TEMP_HIGH",
                        _resultKey_temp,
                        nullptr,
                        "CPU temperature returned to normal"
                    );
                }

                uint32_t throttleState = 0;
                if(getRaspberryPiThrottleState(throttleState)) {
                    uint32_t activeThrottleBits =
                        RPI_THROTTLE_UNDERVOLT_NOW |
                        RPI_THROTTLE_FREQ_CAPPED_NOW |
                        RPI_THROTTLE_THROTTLED_NOW |
                        RPI_THROTTLE_SOFT_TEMP_NOW;

                    if((throttleState & activeThrottleBits) != 0) {
                        IncidentMgr::shared()->raise(
                            CPUTEMP_DEVICE_ID,
                            IncidentMgr::Severity::Critical,
                            "CPU_THROTTLED",
                            _resultKey_temp,
                            nullptr,
                            "Raspberry Pi reports active throttling"
                        );
                    }
                    else {
                        IncidentMgr::shared()->clear(
                            CPUTEMP_DEVICE_ID,
                            "CPU_THROTTLED",
                            _resultKey_temp,
                            nullptr,
                            "Raspberry Pi reports no active throttling"
                        );
                    }
                }

                if(getFanState(fanState)){
                    results[_resultKey_cooler] = to_string(fanState);

                    IncidentMgr::shared()->clear(
                        CPUTEMP_DEVICE_ID,
                        "DEVICE_IO_FAILED",
                        _resultKey_cooler,
                        nullptr,
                        "CPUTEMP cooler state read succeeded"
                    );
                }
                else {
                    IncidentMgr::shared()->raise(
                        CPUTEMP_DEVICE_ID,
                        IncidentMgr::Severity::Warning,
                        "DEVICE_IO_FAILED",
                        _resultKey_cooler,
                        nullptr,
                        "CPUTEMP cooler state read failed"
                    );
                }

                hasData = true;
           }
           else {
                IncidentMgr::shared()->raise(
                    CPUTEMP_DEVICE_ID,
                    IncidentMgr::Severity::Error,
                    "DEVICE_IO_FAILED",
                    _resultKey_temp,
                    nullptr,
                    "CPUTEMP temperature read failed"
                );
           }
        }
    }
    return hasData;
}

/**
 * @brief Read CPU temperature from Linux thermal sysfs.
 *
 * The kernel reports the value in millidegrees Celsius. This function converts
 * it to degrees Celsius.
 *
 * @param tempOut Receives CPU temperature in degrees Celsius.
 * @return true if the sysfs file was read and parsed successfully.
 */
bool CPUTEMP_Device::getCPUTemp(double & tempOut) {
    bool didSucceed = false;

    try{
        std::ifstream   ifs;
        ifs.open("/sys/class/thermal/thermal_zone0/temp", ios::in);
        if( ifs.is_open()){

            string val;
            ifs >> val;
            ifs.close();
            double temp = std::stod(val);
            temp = temp /1000.0;
            tempOut = temp;
            didSucceed = true;
        }
        // debug
        else {

//            time_t when = time(NULL);
//            tempOut =  (when % 100);
//            didSucceed = true;
        }
    }

    catch(std::ifstream::failure &err) {
    }
      return didSucceed;
}

/**
 * @brief Read Raspberry Pi cooler/fan state from Linux thermal sysfs.
 *
 * The cooler state is typically 0 when off and greater than 0 when active,
 * depending on the cooling driver.
 *
 * @param state Receives the raw cooling device state.
 * @return true if the sysfs file was read and parsed successfully.
 */
bool CPUTEMP_Device::getFanState(uint8_t  &state) {
    bool didSucceed = false;

    try{
        std::ifstream   ifs;
        ifs.open("/sys/class/thermal/cooling_device0/cur_state", ios::in);
        if( ifs.is_open()){

            string val;
            ifs >> val;
            ifs.close();
            state = std::stoi(val);

            didSucceed = true;
        }
    }

    catch(std::ifstream::failure &err) {
    }
      return didSucceed;
}
