//
//  POWERCONTROL_Device.cpp
//  pIoTServer
//
//  Created by vinnie on 5/30/26.
//

#include "POWERCONTROL_Device.hpp"

#include "LogMgr.hpp"
#include "PropValKeys.hpp"

#include <errno.h>
#include <string.h>

constexpr string_view Driver_Version = "1.0.0 dev 4";

static constexpr const char* KEY_POWER_AC_OK          = "POWER_AC_OK";
static constexpr const char* KEY_POWER_STATUS         = "POWER_DIAGNOSTIC_STATUS";
static constexpr const char* KEY_POWER_WAKE_TIMER_MIN = "POWER_WAKE_TIMER_MIN";

bool POWERCONTROL_Device::getVersion(string &str)
{
    str = string(Driver_Version);
    return true;
}

POWERCONTROL_Device::POWERCONTROL_Device(string devID)
    : POWERCONTROL_Device(devID, string())
{
}

POWERCONTROL_Device::POWERCONTROL_Device(string devID, string driverName)
{
    setDeviceID(devID, driverName);

    _state = INS_UNKNOWN;
    _lastQueryTime = {0,0};

    json j = {
        { PROP_DEVICE_MFG_PART, "ATmega88PB Power Controller" },
        { PROP_DESCRIPTION, "Read-only I2C power controller status interface, one-byte protocol." },
        { PROP_OTHER, {
            { "i2c_default_address", "0x08" },
            { "protocol", "one-byte bare read status" },
            { "status_read", "i2ctransfer -y 1 r1@0x08" },
            { "register_pointer_reads", "disabled" },
            { "repeated_start_reads", "disabled" },
            { "multi_byte_operations", "disabled" },
            { "wake_timer_source", "decoded from status byte bits 4-6" },
            { "relay_control", "disabled" },
            { "shutdown_control", "disabled" },
            { "wake_timer_control", "disabled" },
            { "led_control", "disabled" }
        }}
    };

    setProperties(j);

    _deviceState = DEVICE_STATE_UNKNOWN;
    _isSetup = false;
}

POWERCONTROL_Device::~POWERCONTROL_Device()
{
    stop();
}

bool POWERCONTROL_Device::initWithSchema(deviceSchemaMap_t deviceSchema)
{
    uint64_t delay = UINT64_MAX;

    for(const auto& [key, entry] : deviceSchema) {

        if(key == KEY_POWER_AC_OK) {
            _resultKey_acOK = key;
            if(entry.queryDelay < delay) delay = entry.queryDelay;
            _isSetup = true;
        }
        else if(key == KEY_POWER_STATUS) {
            _resultKey_status = key;
            if(entry.queryDelay < delay) delay = entry.queryDelay;
            _isSetup = true;
        }
        else if(key == KEY_POWER_WAKE_TIMER_MIN) {
            _resultKey_wakeTimerMin = key;
            if(entry.queryDelay < delay) delay = entry.queryDelay;
            _isSetup = true;
        }
    }

    _queryDelay = delay != UINT64_MAX ? delay : default_queryDelay;

    _deviceState = DEVICE_STATE_DISCONNECTED;
    return _isSetup;
}

bool POWERCONTROL_Device::start()
{
    bool status = false;
    int error = 0;

    if(!_deviceProperties[PROP_ADDRESS].is_string()) {
        LOGT_DEBUG("POWERCONTROL_Device begin called with no %s property",
                   string(PROP_ADDRESS).c_str());
        return false;
    }

    if(_deviceID.size() == 0) {
        LOGT_DEBUG("POWERCONTROL_Device has no deviceID");
        return false;
    }

    string address = _deviceProperties[PROP_ADDRESS];
    uint8_t i2cAddr = std::stoi(address.c_str(), 0, 16);

    if(!_isSetup) {
        LOGT_DEBUG("POWERCONTROL_Device(%s) begin called before initWithSchema",
                   address.c_str());
        return false;
    }

    LOGT_DEBUG("POWERCONTROL_Device(%02X) begin", i2cAddr);

    status = _device.begin(i2cAddr, error);

    if(!status) {
        LOGT_ERROR("POWERCONTROL_Device(%02X) begin FAILED: %s",
                   i2cAddr,
                   strerror(errno));

        _state = INS_INVALID;
        _deviceState = DEVICE_STATE_ERROR;
        return false;
    }

    /*
     * Do not perform an immediate I2C status read here.
     *
     * Startup is the worst time to add extra bus traffic. The manager is
     * starting all configured I2C devices, and VALVEMASTER may also start its
     * action thread. The first POWERCONTROL status read should happen later,
     * after the configured query interval has elapsed.
     *
     * This plugin is read-only during normal operation. It must never send
     * shutdown, cancel, wake preset, LED, relay, reset, sleep, or all-off
     * commands to the AVR from start().
     */
    gettimeofday(&_lastQueryTime, NULL);

    _state = INS_IDLE;
    _deviceState = DEVICE_STATE_CONNECTED;

    LOGT_INFO("POWERCONTROL_Device(%02X) started read-only, first poll in %llu second(s)",
              i2cAddr,
              static_cast<unsigned long long>(_queryDelay));

    return true;
}

void POWERCONTROL_Device::stop()
{
    LOGT_DEBUG("POWERCONTROL_Device(%02X) stop", _device.getDevAddr());

    _state = INS_UNKNOWN;
    _lastQueryTime = {0,0};

    /*
     * stop() only releases the local I2C object.
     *
     * It must not write anything to the AVR. Manager shutdown, sequence abort,
     * generic cleanup, and plugin unload are not permission to cut Pi power.
     */
    if(_device.isOpen()) {
        _device.stop();
    }

    _deviceState = DEVICE_STATE_DISCONNECTED;
}

bool POWERCONTROL_Device::setEnabled(bool enable)
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

bool POWERCONTROL_Device::isConnected()
{
    return _device.isOpen();
}

bool POWERCONTROL_Device::allOff()
{
    /*
     * POWERCONTROL is read-only from this device wrapper.
     *
     * allOff() must be a no-op. The AVR power controller must never interpret
     * manager shutdown, sequence abort, global cleanup, or generic allOff()
     * behavior as permission to turn off the Pi power relay.
     */
    return true;
}

bool POWERCONTROL_Device::getValues(keyValueMap_t &results)
{
    bool hasData = false;

    if(!isEnabled()) {
        return false;
    }

    if(!isConnected()) {
        return false;
    }

    if(_state != INS_IDLE) {
        return false;
    }

    bool shouldQuery = false;

    if(_lastQueryTime.tv_sec == 0 && _lastQueryTime.tv_usec == 0) {
        shouldQuery = true;
    }
    else {
        timeval now, diff;
        gettimeofday(&now, NULL);
        timersub(&now, &_lastQueryTime, &diff);

        if(diff.tv_sec >= 0 && static_cast<uint64_t>(diff.tv_sec) >= _queryDelay) {
            shouldQuery = true;
        }
    }

    if(!shouldQuery) {
        return false;
    }

    /*
     * Update query time before touching I2C.
     *
     * If the bus is unhappy, this prevents POWERCONTROL from retrying every
     * manager loop and making a bad bus situation worse.
     */
    gettimeofday(&_lastQueryTime, NULL);

    POWERCONTROL::POWERCONTROL_data data = {};

    if(_device.readStatus(data)) {

        if(!_resultKey_acOK.empty()) {
            results[_resultKey_acOK] = data.acOK ? "true" : "false";
        }

        if(!_resultKey_status.empty()) {
            results[_resultKey_status] = to_string(data.statusByte);
        }

        /*
         * This is the stored wake preset decoded from the status byte.
         * It is not a live countdown.
         */
        if(!_resultKey_wakeTimerMin.empty()) {
            results[_resultKey_wakeTimerMin] = to_string(data.wakeTimerMin);
        }

        _state = INS_IDLE;
        _deviceState = DEVICE_STATE_CONNECTED;
        hasData = true;
    }
    else {
        LOGT_ERROR("POWERCONTROL_Device(%02X) readStatus FAILED",
                   _device.getDevAddr());

        /*
         * A single I2C miss on a shared bus should not poison the device.
         *
         * Do not stop the low-level I2C object here.
         * Do not mark the device invalid.
         * Do not send any recovery command to the AVR.
         *
         * Just report disconnected for this sample and try again after the next
         * configured interval.
         */
        _deviceState = DEVICE_STATE_DISCONNECTED;
    }

    return hasData;
}

bool POWERCONTROL_Device::setValues(keyValueMap_t kv)
{
    (void)kv;

    /*
     * Read-only driver.
     *
     * Do not add relay, sleep, shutdown, reset, wake timer, LED, or power-off
     * writes through setValues().
     */
    LOGT_ERROR("POWERCONTROL_Device setValues rejected: device is read-only");
    return false;
}
