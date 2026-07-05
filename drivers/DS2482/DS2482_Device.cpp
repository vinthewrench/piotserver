//
//  DS2482_Device.cpp
//  pIoTServer
//
//  pIoTServer device wrapper for DS2482 I2C-to-1Wire bridge.
//
//  First-pass support:
//      - DS18B20 temperature sensors only
//      - pins[].address contains the DS18B20 1-Wire ROM/address
//

#include "DS2482_Device.hpp"

#include "IncidentMgr.hpp"
#include "LogMgr.hpp"
#include "PropValKeys.hpp"
#include "TimeStamp.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <sstream>

constexpr string_view Driver_Version = "1.0.0 dev 0";

bool DS2482_Device::getVersion(string &str)
{
    str = string(Driver_Version);
    return true;
}

DS2482_Device::DS2482_Device(string devID)
: DS2482_Device(devID, string())
{
}

DS2482_Device::DS2482_Device(string devID, string driverName)
{
    setDeviceID(devID, driverName);

    _state = INS_UNKNOWN;
    _deviceState = DEVICE_STATE_UNKNOWN;

    _lastQueryTime = {0, 0};
    _queryDelay = default_queryDelay;
    _isSetup = false;

    json j = {
        { PROP_DEVICE_MFG_URL, "https://www.analog.com/en/products/ds2482-100.html" },
        { PROP_DEVICE_MFG_PART, "Analog Devices DS2482 I2C-to-1Wire bridge" },
    };

    setProperties(j);
}

DS2482_Device::~DS2482_Device()
{
    stop();
}

bool DS2482_Device::initWithSchema(deviceSchemaMap_t deviceSchema)
{
    _configuredValues.clear();
    _queryDelay = default_queryDelay;
    _isSetup = false;

    uint64_t minimumQueryDelay = UINT64_MAX;

    for(const auto &[key, entry] : deviceSchema) {
        if(entry.units != DEGREES_C) {
            continue;
        }

        string sensorAddress;

        if(!extractSensorAddress(entry, sensorAddress)) {
            LOGT_ERROR("DS2482_Device schema entry '%s' missing DS18B20 address", key.c_str());
            continue;
        }

        array<uint8_t, 8> rom = {};

        if(!DS2482::stringToRom(sensorAddress, rom)) {
            LOGT_ERROR("DS2482_Device schema entry '%s' has invalid DS18B20 address '%s'",
                       key.c_str(),
                       sensorAddress.c_str());
            continue;
        }

        if(rom[0] != 0x28) {
            LOGT_ERROR("DS2482_Device schema entry '%s' address '%s' is not DS18B20 family 0x28",
                       key.c_str(),
                       sensorAddress.c_str());
            continue;
        }

        DS18B20Value_t value;
        value.key = key;
        value.address = DS2482::romToString(rom);
        value.rom = rom;
        value.queryDelay = entry.queryDelay != UINT64_MAX ? entry.queryDelay : default_queryDelay;

        _configuredValues.push_back(value);

        if(value.queryDelay < minimumQueryDelay) {
            minimumQueryDelay = value.queryDelay;
        }
    }

    if(_configuredValues.empty()) {
        LOGT_ERROR("DS2482_Device initWithSchema found no configured DS18B20 temperature values");
        _deviceState = DEVICE_STATE_DISCONNECTED;
        return false;
    }

    _queryDelay = minimumQueryDelay != UINT64_MAX ? minimumQueryDelay : default_queryDelay;
    _isSetup = true;

    LOGT_DEBUG("DS2482_Device configured %zu DS18B20 value(s), queryDelay=%llu",
               _configuredValues.size(),
               static_cast<unsigned long long>(_queryDelay));

    return true;
}

bool DS2482_Device::start()
{
    bool status = false;
    int error = 0;

    if(!_deviceProperties[PROP_ADDRESS].is_string()) {
        LOGT_DEBUG("DS2482_Device begin called with no %s property", string(PROP_ADDRESS).c_str());
        return false;
    }

    if(_deviceID.empty()) {
        LOGT_DEBUG("DS2482_Device has no deviceID");
        return false;
    }

    string address = _deviceProperties[PROP_ADDRESS];
    uint8_t i2cAddr = static_cast<uint8_t>(std::stoi(address.c_str(), nullptr, 16));

    if(!_isSetup) {
        LOGT_DEBUG("DS2482_Device(%s) begin called before initWithSchema", address.c_str());
        return false;
    }

    LOGT_DEBUG("DS2482_Device(%02X) begin with %zu DS18B20 value(s)",
               i2cAddr,
               _configuredValues.size());

    status = _device.begin(i2cAddr, error);

    if(status) {
        _lastQueryTime = {0, 0};
        _state = INS_IDLE;
        _deviceState = DEVICE_STATE_CONNECTED;

        for(const auto &value : _configuredValues) {
            clearValueIncident(value.key, "DS2482 begin succeeded");
        }
    }
    else {
        LOGT_ERROR("DS2482_Device(%02X) begin FAILED: %s",
                   i2cAddr,
                   strerror(error ? error : errno));

        _state = INS_INVALID;
        _deviceState = DEVICE_STATE_ERROR;

        for(const auto &value : _configuredValues) {
            raiseValueIncident(value.key, "DS2482 begin failed");
        }
    }

    return status;
}

void DS2482_Device::stop()
{
    LOGT_DEBUG("DS2482_Device(%02X) stop", _device.getDevAddr());

    _state = INS_UNKNOWN;
    _lastQueryTime = {0, 0};

    if(_device.isOpen()) {
        _device.stop();
    }

    _deviceState = DEVICE_STATE_DISCONNECTED;
}

bool DS2482_Device::setEnabled(bool enable)
{
    if(enable) {
        _isEnabled = true;

        if(_deviceState == DEVICE_STATE_CONNECTED) {
            return true;
        }

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

bool DS2482_Device::isConnected()
{
    return _device.isOpen();
}

bool DS2482_Device::getValues(keyValueMap_t &results)
{
    bool hasData = false;

    if(!_isEnabled) {
        return false;
    }

    if(!isConnected()) {
        return false;
    }

    if(_state != INS_IDLE) {
        return false;
    }

    if(!shouldQuery()) {
        return false;
    }

    int error = 0;
    vector<DS2482::Temperature> readings;

    if(!_device.readTemps(readings, error)) {
        LOGT_ERROR("DS2482_Device(%02X) readTemps FAILED: %s",
                   _device.getDevAddr(),
                   strerror(error ? error : errno));

        for(const auto &value : _configuredValues) {
            raiseValueIncident(value.key, "DS2482 temperature read failed");
        }

        _deviceState = DEVICE_STATE_ERROR;
        return false;
    }

    map<string, DS2482::Temperature> readingsByAddress;

    for(const auto &reading : readings) {
        readingsByAddress[DS2482::romToString(reading.rom)] = reading;
    }

    for(const auto &value : _configuredValues) {
        auto it = readingsByAddress.find(value.address);

        if(it == readingsByAddress.end()) {
            LOGT_ERROR("DS2482_Device(%02X) configured DS18B20 missing: key=%s address=%s",
                       _device.getDevAddr(),
                       value.key.c_str(),
                       value.address.c_str());

            raiseValueIncident(value.key, "Configured DS18B20 sensor missing");
            continue;
        }

        const DS2482::Temperature &reading = it->second;

        if(!reading.success) {
            string message = reading.errorText.empty()
                           ? string("DS18B20 temperature read failed")
                           : reading.errorText;

            LOGT_ERROR("DS2482_Device(%02X) DS18B20 read failed: key=%s address=%s error=%s",
                       _device.getDevAddr(),
                       value.key.c_str(),
                       value.address.c_str(),
                       message.c_str());

            raiseValueIncident(value.key, message);
            continue;
        }

        results[value.key] = to_string(reading.tempC);
        clearValueIncident(value.key, "DS18B20 temperature read succeeded");
        hasData = true;
    }

    gettimeofday(&_lastQueryTime, nullptr);

    if(hasData) {
        _deviceState = DEVICE_STATE_CONNECTED;
    }

    return hasData;
}

bool DS2482_Device::shouldQuery()
{
    if(_lastQueryTime.tv_sec == 0 && _lastQueryTime.tv_usec == 0) {
        return true;
    }

    timeval now;
    timeval diff;

    gettimeofday(&now, nullptr);
    timersub(&now, &_lastQueryTime, &diff);

    if(diff.tv_sec >= 0 && static_cast<uint64_t>(diff.tv_sec) >= _queryDelay) {
        return true;
    }

    return false;
}

bool DS2482_Device::extractSensorAddress(const deviceSchema_t &entry, string &address)
{
    address.clear();

    if(!entry.otherProps.is_object()) {
        return false;
    }

    /*
     * Current schema:
     *
     *     pins[].other.props.address = "28-..."
     *
     * The DS18B20 ROM code is the sensor's address on the 1-Wire bus.
     * Keep it in other.props for now because the existing schema parser
     * does not expose pin-level address as a dedicated deviceSchema_t field.
     *
     * Accept "rom" too while shaking this out, but official config should use
     * "address".
     */
    if(entry.otherProps.contains("address") &&
       entry.otherProps["address"].is_string()) {
        address = entry.otherProps["address"].get<string>();
        return !address.empty();
    }

    if(entry.otherProps.contains("rom") &&
       entry.otherProps["rom"].is_string()) {
        address = entry.otherProps["rom"].get<string>();
        return !address.empty();
    }

    return false;
}

void DS2482_Device::raiseValueIncident(const string &key, const string &message)
{
    IncidentMgr::shared()->raise(
        _deviceID,
        IncidentMgr::Severity::Error,
        "DEVICE_IO_FAILED",
        key,
        nullptr,
        message.c_str()
    );
}

void DS2482_Device::clearValueIncident(const string &key, const string &message)
{
    IncidentMgr::shared()->clear(
        _deviceID,
        "DEVICE_IO_FAILED",
        key,
        nullptr,
        message.c_str()
    );
}
