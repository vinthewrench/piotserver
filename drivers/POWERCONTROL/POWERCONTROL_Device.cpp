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

constexpr string_view Driver_Version = "1.0.0 dev 0";

static constexpr const char* KEY_POWER_AC_OK          = "POWER_AC_OK";
static constexpr const char* KEY_POWER_STATUS         = "POWER_STATUS";
static constexpr const char* KEY_POWER_FW_VERSION     = "POWER_FW_VERSION";
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
        { PROP_DESCRIPTION, "I2C power controller status interface." },
        { PROP_OTHER, {
            { "status_register", "0xF0" },
            { "i2c_default_address", "0x08" }
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
        else if(key == KEY_POWER_FW_VERSION) {
            _resultKey_fwVersion = key;
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

    POWERCONTROL::POWERCONTROL_data data = {};

    status = _device.readStatus(data);
    if(!status) {
        LOGT_ERROR("POWERCONTROL_Device(%02X) readStatus FAILED: %s",
                   i2cAddr,
                   strerror(errno));
        _state = INS_INVALID;
        _deviceState = DEVICE_STATE_ERROR;
        return false;
    }

    LOGT_INFO("POWERCONTROL_Device(%02X) fw=0x%02X status=0x%02X wake_timer=%u ac_ok=%s",
              i2cAddr,
              data.firmwareVersion,
              data.statusByte,
              data.wakeTimerMin,
              data.acOK ? "true" : "false");

    _lastQueryTime = {0,0};
    _state = INS_IDLE;
    _deviceState = DEVICE_STATE_CONNECTED;

    return true;
}

void POWERCONTROL_Device::stop()
{
    LOGT_DEBUG("POWERCONTROL_Device(%02X) stop", _device.getDevAddr());

    _state = INS_UNKNOWN;
    _lastQueryTime = {0,0};

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

bool POWERCONTROL_Device::getValues(keyValueMap_t &results)
{
    bool hasData = false;

    if(!isConnected()) {
        return false;
    }

    if(_state == INS_IDLE) {

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

        if(shouldQuery) {
            POWERCONTROL::POWERCONTROL_data data = {};

            if(_device.readStatus(data)) {

                if(!_resultKey_acOK.empty()) {
                    results[_resultKey_acOK] = data.acOK ? "true" : "false";
                }

                if(!_resultKey_status.empty()) {
                    results[_resultKey_status] = to_string(data.statusByte);
                }

                if(!_resultKey_fwVersion.empty()) {
                    results[_resultKey_fwVersion] = to_string(data.firmwareVersion);
                }

                if(!_resultKey_wakeTimerMin.empty()) {
                    results[_resultKey_wakeTimerMin] = to_string(data.wakeTimerMin);
                }

                gettimeofday(&_lastQueryTime, NULL);
                hasData = true;
            }
            else {
                LOGT_ERROR("POWERCONTROL_Device(%02X) readStatus FAILED",
                           _device.getDevAddr());

                _state = INS_INVALID;
                _deviceState = DEVICE_STATE_ERROR;
                _device.stop();
            }
        }
    }

    return hasData;
}

bool POWERCONTROL_Device::setValues(keyValueMap_t kv)
{
    (void)kv;

    LOGT_ERROR("POWERCONTROL_Device setValues rejected: device is read-only");
    return false;
}
