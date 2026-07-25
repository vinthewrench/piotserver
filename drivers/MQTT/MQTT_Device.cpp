//
//  MQTT_Device.cpp
//  pIoTServer MQTT plugin device
//

#include "MQTT_Device.hpp"

#include "IncidentMgr.hpp"
#include "LogMgr.hpp"
#include "PropValKeys.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

constexpr std::string_view Driver_Version = "1.0.0 dev 0";

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string uppercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

bool jsonStringProperty(const nlohmann::json& properties,
                        const char* key,
                        std::string& valueOut)
{
    if(!properties.contains(key) || !properties[key].is_string()) {
        return false;
    }

    valueOut = properties[key].get<std::string>();
    return true;
}

bool jsonIntProperty(const nlohmann::json& properties,
                     const char* key,
                     int& valueOut)
{
    if(!properties.contains(key) || !properties[key].is_number_integer()) {
        return false;
    }

    valueOut = properties[key].get<int>();
    return true;
}

bool isIntegerUnit(valueSchemaUnits_t units)
{
    switch(units) {
        case INT:
        case SECONDS:
        case MINUTES:
        case TIME_T:
            return true;

        default:
            return false;
    }
}

bool isNumericUnit(valueSchemaUnits_t units)
{
    switch(units) {
        case INT:
        case MAH:
        case PERCENT:
        case WATTS:
        case MILLIVOLTS:
        case MILLIAMPS:
        case SECONDS:
        case MINUTES:
        case DEGREES_C:
        case VOLTS:
        case HERTZ:
        case AMPS:
        case RH:
        case HPA:
        case TIME_T:
        case FLOAT:
        case LUX:
            return true;

        default:
            return false;
    }
}

struct rgb_t {
    int red = 0;
    int green = 0;
    int blue = 0;
};

rgb_t parseColor(const std::string& input)
{
    static const std::map<std::string, rgb_t> names = {
        {"black", {0, 0, 0}},
        {"blue", {0, 0, 255}},
        {"cyan", {0, 255, 255}},
        {"green", {0, 255, 0}},
        {"magenta", {255, 0, 255}},
        {"orange", {255, 165, 0}},
        {"purple", {128, 0, 128}},
        {"red", {255, 0, 0}},
        {"white", {255, 255, 255}},
        {"yellow", {255, 255, 0}},
    };

    const std::string normalized = lowercase(Utils::trim(input));
    const auto named = names.find(normalized);
    if(named != names.end()) {
        return named->second;
    }

    std::string hex = normalized;
    if(!hex.empty() && hex.front() == '#') {
        hex.erase(hex.begin());
    }
    else if(hex.size() >= 2 && hex[0] == '0' && hex[1] == 'x') {
        hex.erase(0, 2);
    }

    if(hex.size() != 6 ||
       !std::all_of(hex.begin(), hex.end(), [](unsigned char c) {
           return std::isxdigit(c) != 0;
       })) {
        throw std::runtime_error(
            "color must be #RRGGBB, 0xRRGGBB, or a common color name");
    }

    const unsigned long value = std::stoul(hex, nullptr, 16);
    return {
        static_cast<int>((value >> 16U) & 0xFFU),
        static_cast<int>((value >> 8U) & 0xFFU),
        static_cast<int>(value & 0xFFU),
    };
}

std::string compactNumber(double value)
{
    std::ostringstream stream;
    stream << std::setprecision(12) << value;
    return stream.str();
}

} // namespace

MQTT_Device::MQTT_Device(std::string devID)
    : MQTT_Device(std::move(devID), std::string())
{
}

MQTT_Device::MQTT_Device(std::string devID, std::string driverName)
{
    setDeviceID(std::move(devID), std::move(driverName));

    json properties = {
        {PROP_DESCRIPTION,
         "MQTT broker plugin with subscribed state caching and confirmed writes"},
        {PROP_DEVICE_MFG_PART, "MQTT / Zigbee2MQTT"},
    };
    setProperties(properties);

    _deviceState = DEVICE_STATE_UNKNOWN;
}

MQTT_Device::~MQTT_Device()
{
    stop();
}

bool MQTT_Device::getVersion(std::string& version)
{
    version = std::string(Driver_Version);
    return true;
}

void MQTT_Device::getProperties(nlohmann::json& properties)
{
    pIoTServerDevice::getProperties(properties);

    if(properties.contains("mqtt.password")) {
        properties["mqtt.password"] = "********";
    }
}

bool MQTT_Device::initWithSchema(deviceSchemaMap_t deviceSchema)
{
    _bindings.clear();
    _topicKeys.clear();

    for(const auto& [key, entry] : deviceSchema) {
        if(!entry.otherProps.is_object()) {
            LOGT_ERROR("MQTT_Device key \"%s\" has no other.props object",
                       key.c_str());
            return false;
        }

        const json& properties = entry.otherProps;
        std::string target;
        if(!jsonStringProperty(properties, "mqtt.target", target) ||
           Utils::trim(target).empty()) {
            LOGT_ERROR("MQTT_Device key \"%s\" has no mqtt.target",
                       key.c_str());
            return false;
        }
        target = Utils::trim(target);

        std::string property;
        jsonStringProperty(properties, "mqtt.property", property);
        property = Utils::trim(property);

        if(property.empty() && entry.units == BOOL) {
            property = "state";
        }

        if(property.empty()) {
            LOGT_ERROR("MQTT_Device key \"%s\" requires mqtt.property",
                       key.c_str());
            return false;
        }

        binding_t binding;
        binding.key = key;
        binding.target = target;
        binding.property = property;
        binding.units = entry.units;
        binding.readOnly = entry.readOnly;

        std::string targetType;
        if(jsonStringProperty(properties, "mqtt.target_type", targetType)) {
            binding.targetType = lowercase(Utils::trim(targetType));
        }

        if(binding.targetType != "device" && binding.targetType != "group") {
            LOGT_ERROR("MQTT_Device key \"%s\" has invalid mqtt.target_type \"%s\"",
                       key.c_str(),
                       binding.targetType.c_str());
            return false;
        }

        if(properties.contains("mqtt.on")) {
            binding.onValue = properties["mqtt.on"];
        }
        if(properties.contains("mqtt.off")) {
            binding.offValue = properties["mqtt.off"];
        }
        if(properties.contains("mqtt.safe_value")) {
            binding.safeValue = properties["mqtt.safe_value"];
        }

        _bindings[key] = binding;
    }

    _isSetup = !_bindings.empty();
    _deviceState = _isSetup
        ? DEVICE_STATE_DISCONNECTED
        : DEVICE_STATE_ERROR;

    return _isSetup;
}

std::string localHostname()
{
    std::array<char, 256> hostname{};

    if(::gethostname(hostname.data(), hostname.size() - 1) != 0) {
        return "unknown";
    }

    hostname.back() = '\0';

    std::string result = Utils::trim(hostname.data());
    if(result.empty()) {
        return "unknown";
    }

    std::transform(result.begin(),
                   result.end(),
                   result.begin(),
                   [](unsigned char character) {
                       if(std::isalnum(character) ||
                          character == '-' ||
                          character == '_') {
                           return static_cast<char>(std::tolower(character));
                       }

                       return '-';
                   });

    return result;
}

bool MQTT_Device::parseDeviceConfig(MQTT::config_t& configOut,
                                    std::string& errorOut)
{
    errorOut.clear();

    if(!jsonStringProperty(_deviceProperties, "mqtt.host", configOut.host) ||
       Utils::trim(configOut.host).empty()) {
        errorOut = "mqtt.host is required";
        return false;
    }
    configOut.host = Utils::trim(configOut.host);

    int value = 0;
    if(_deviceProperties.contains("mqtt.port")) {
        if(!jsonIntProperty(_deviceProperties, "mqtt.port", value)) {
            errorOut = "mqtt.port must be an integer";
            return false;
        }
        configOut.port = value;
    }

    if(jsonStringProperty(_deviceProperties, "mqtt.base_topic", _baseTopic)) {
        _baseTopic = Utils::trim(_baseTopic);
    }
    else {
        _baseTopic = "zigbee2mqtt";
    }

    while(!_baseTopic.empty() && _baseTopic.front() == '/') {
        _baseTopic.erase(_baseTopic.begin());
    }

    while(!_baseTopic.empty() && _baseTopic.back() == '/') {
        _baseTopic.pop_back();
    }

    if(_baseTopic.empty()) {
        errorOut = "mqtt.base_topic cannot be empty";
        return false;
    }

    jsonStringProperty(_deviceProperties,
                       "mqtt.username",
                       configOut.username);

    jsonStringProperty(_deviceProperties,
                       "mqtt.password",
                       configOut.password);

    jsonStringProperty(_deviceProperties,
                       "mqtt.client_id",
                       configOut.clientID);

    configOut.username = Utils::trim(configOut.username);
    configOut.clientID = Utils::trim(configOut.clientID);

    if(configOut.clientID.empty()) {
        configOut.clientID = "piotserver-mqtt-" + localHostname();
    }

    if(_deviceProperties.contains("mqtt.keepalive")) {
        if(!jsonIntProperty(_deviceProperties, "mqtt.keepalive", value)) {
            errorOut = "mqtt.keepalive must be an integer";
            return false;
        }
        configOut.keepAlive = value;
    }

    if(_deviceProperties.contains("mqtt.qos")) {
        if(!jsonIntProperty(_deviceProperties, "mqtt.qos", value)) {
            errorOut = "mqtt.qos must be an integer";
            return false;
        }
        configOut.qos = value;
    }

    if(_deviceProperties.contains("mqtt.timeout_ms")) {
        if(!jsonIntProperty(_deviceProperties, "mqtt.timeout_ms", value) ||
           value < 1) {
            errorOut = "mqtt.timeout_ms must be a positive integer";
            return false;
        }

        configOut.timeout = std::chrono::milliseconds(value);
    }

    return true;
}


bool MQTT_Device::start()
{
    if(!_isSetup) {
        LOGT_ERROR("MQTT_Device(%s) start called before initWithSchema",
                   _deviceID.c_str());
        return false;
    }

    if(_deviceID.empty()) {
        LOGT_ERROR("MQTT_Device has no deviceID");
        return false;
    }

    stop();

    MQTT::config_t config;
    std::string error;
    if(!parseDeviceConfig(config, error)) {
        _deviceState = DEVICE_STATE_ERROR;
        reportFailure(_deviceID, error);
        LOGT_ERROR("MQTT_Device(%s) configuration failed: %s",
                   _deviceID.c_str(),
                   error.c_str());
        return false;
    }

    _topicKeys.clear();
    for(const auto& [key, binding] : _bindings) {
        _topicKeys[stateTopic(binding.target)].push_back(key);
    }

    _mqtt = std::make_unique<MQTT>();
    const bool connected = _mqtt->begin(
        config,
        [this](const std::string& topic, const json& payload) {
            handleMQTTMessage(topic, payload);
        },
        [this](bool isConnectedNow, const std::string& detail) {
            handleMQTTConnection(isConnectedNow, detail);
        },
        error);

    if(!connected) {
        _deviceState = DEVICE_STATE_ERROR;
        reportFailure(_deviceID, error);
        LOGT_ERROR("MQTT_Device(%s) connect failed: %s",
                   _deviceID.c_str(),
                   error.c_str());
        _mqtt.reset();
        return false;
    }

    for(const auto& [topic, keys] : _topicKeys) {
        (void)keys;
        if(!_mqtt->subscribe(topic, error)) {
            _deviceState = DEVICE_STATE_ERROR;
            reportFailure(_deviceID, error);
            LOGT_ERROR("MQTT_Device(%s) subscribe failed: %s",
                       _deviceID.c_str(),
                       error.c_str());
            _mqtt->stop();
            _mqtt.reset();
            return false;
        }
    }

    _deviceState = DEVICE_STATE_CONNECTED;
    clearFailure(_deviceID, "MQTT broker connection succeeded");
    requestInitialState();

    LOGT_INFO("MQTT_Device(%s) connected to %s:%d, client_id=%s, base_topic=%s, bindings=%zu",
              _deviceID.c_str(),
              config.host.c_str(),
              config.port,
              config.clientID.c_str(),
              _baseTopic.c_str(),
              _bindings.size());

    return true;
}

void MQTT_Device::stop()
{
    if(_mqtt) {
        _mqtt->stop();
        _mqtt.reset();
    }

    _deviceState = DEVICE_STATE_DISCONNECTED;
}

bool MQTT_Device::isConnected()
{
    return _mqtt != nullptr && _mqtt->isConnected();
}

bool MQTT_Device::setEnabled(bool enable)
{
    if(enable) {
        _isEnabled = true;
        if(isConnected()) {
            return true;
        }
        return start();
    }

    _isEnabled = false;
    bool success = true;
    if(isConnected()) {
        success = allOff();
    }
    stop();
    return success;
}

bool MQTT_Device::hasUpdates()
{
    std::lock_guard<std::mutex> lock(_cacheMutex);
    return !_pendingValues.empty();
}

bool MQTT_Device::getValues(keyValueMap_t& results)
{
    std::lock_guard<std::mutex> lock(_cacheMutex);

    if(_pendingValues.empty()) {
        return false;
    }

    results = _pendingValues;
    _pendingValues.clear();
    return !results.empty();
}

std::string MQTT_Device::stateTopic(const std::string& target) const
{
    return _baseTopic + "/" + target;
}

std::string MQTT_Device::setTopic(const std::string& target) const
{
    return stateTopic(target) + "/set";
}

std::string MQTT_Device::getTopic(const std::string& target) const
{
    return stateTopic(target) + "/get";
}

void MQTT_Device::requestInitialState()
{
    if(!_mqtt || !_mqtt->isConnected()) {
        return;
    }

    std::map<std::string, json> requests;
    for(const auto& [key, binding] : _bindings) {
        (void)key;
        if(!binding.readOnly) {
            if(binding.units == BRIGHTNESS) {
                requests[binding.target][binding.property] = "";
                requests[binding.target]["brightness"] = "";
            }
            else {
                requests[binding.target][binding.property] = "";
            }
        }
    }

    for(const auto& [target, payload] : requests) {
        std::string error;
        if(!_mqtt->publish(getTopic(target), payload, error)) {
            LOGT_DEBUG("MQTT_Device(%s) initial GET for %s failed: %s",
                       _deviceID.c_str(),
                       target.c_str(),
                       error.c_str());
        }
    }
}

bool MQTT_Device::publishTarget(const std::string& target,
                                const json& payload,
                                bool waitForConfirmation,
                                std::string& errorOut)
{
    if(!_mqtt || !_mqtt->isConnected()) {
        errorOut = "MQTT broker is not connected";
        return false;
    }

    const std::string topic = stateTopic(target);
    const std::uint64_t generation = _mqtt->generationForTopic(topic);

    if(!_mqtt->publish(setTopic(target), payload, errorOut)) {
        return false;
    }

    if(!waitForConfirmation) {
        return true;
    }

    json reported;
    return _mqtt->waitForMessage(
        topic,
        generation,
        [&payload](const json& candidate) {
            return stateMatches(candidate, payload);
        },
        reported,
        errorOut);
}

bool MQTT_Device::setValues(keyValueMap_t values)
{

    if(!isConnected()) {
        reportFailure(_deviceID,
                      "MQTT SET failed: broker disconnected");
        return false;
    }

    std::map<std::string, json> payloads;
    std::map<std::string, std::vector<std::string>> keysByTarget;

    try {
        for(const auto& [key, value] : values) {
            const auto found = _bindings.find(key);
            if(found == _bindings.end() ||
               found->second.readOnly) {
                return false;
            }

            const binding_t& binding = found->second;
            json& payload = payloads[binding.target];

            if(binding.units == BRIGHTNESS) {
                if(binding.property != "state") {
                    throw std::runtime_error(
                        key + " BRIGHTNESS requires mqtt.property=state");
                }

                const std::string trimmed = Utils::trim(value);

                std::size_t consumed = 0;
                double percent = 0.0;
                bool isNumeric = false;

                if(trimmed == "1") {
                    // keyValueMap_t represents Boolean true as "1".
                    percent = 100.0;
                    isNumeric = true;
                }
                else {
                    try {
                        percent = std::stod(trimmed, &consumed);
                        isNumeric =
                            consumed == trimmed.size() &&
                            std::isfinite(percent);
                    }
                    catch(const std::invalid_argument&) {
                        // It may be ON/OFF or TRUE/FALSE.
                    }
                }

                if(isNumeric) {
                    if(percent < 0.0 || percent > 100.0) {
                        throw std::runtime_error(
                            key + " brightness must be between 0 and 100");
                    }

                    if(percent == 0.0) {
                        payload[binding.property] =
                            binding.offValue;
                    }
                    else {
                        payload[binding.property] =
                            binding.onValue;

                        payload["brightness"] =
                            static_cast<int>(
                                std::lround(
                                    (percent / 100.0) * 254.0));
                    }
                }
                else {
                    bool state = false;
                    if(!stringToBool(trimmed, state)) {
                        throw std::runtime_error(
                            key +
                            " brightness requires 0-100, "
                            "ON/OFF, or TRUE/FALSE");
                    }

                    if(state) {
                        payload[binding.property] =
                            binding.onValue;
                        payload["brightness"] = 254;
                    }
                    else {
                        payload[binding.property] =
                            binding.offValue;
                    }
                }
            }
            else if(binding.units == PERCENT &&
                    binding.property == "brightness") {
                const std::string trimmed = Utils::trim(value);

                std::size_t consumed = 0;
                const double percent =
                    std::stod(trimmed, &consumed);

                if(consumed != trimmed.size() ||
                   percent < 0.0 ||
                   percent > 100.0) {
                    throw std::runtime_error(
                        "brightness percentage must be "
                        "between 0 and 100");
                }

                if(percent == 0.0) {
                    payload["state"] = "OFF";
                }
                else {
                    payload["state"] = "ON";
                    payload["brightness"] =
                        static_cast<int>(
                            std::lround(
                                (percent / 100.0) * 254.0));
                }
            }
            else {
                payload[binding.property] =
                    outboundValue(binding, value);
            }

            keysByTarget[binding.target].push_back(key);
        }
    }
    catch(const std::exception& exception) {
        reportFailure(_deviceID, exception.what());

        LOGT_ERROR(
            "MQTT_Device(%s) value conversion failed: %s",
            _deviceID.c_str(),
            exception.what());

        return false;
    }

    if(payloads.empty()) {
        return false;
    }

    bool success = true;

    for(const auto& [target, payload] : payloads) {
        const std::string payloadText = payload.dump();
        LOGT_DEBUG(
            "MQTT_Device(%s) sending SET target=%s payload=%s",
            _deviceID.c_str(),
            target.c_str(),
            payloadText.c_str());

        std::string error;
        const bool published =
            publishTarget(target, payload, true, error);

        for(const auto& key : keysByTarget[target]) {
            if(published) {
                clearFailure(key, "MQTT SET confirmed");
            }
            else {
                reportFailure(key, error);
            }
        }

        if(!published) {
            LOGT_ERROR(
                "MQTT_Device(%s) SET %s failed: %s",
                _deviceID.c_str(),
                target.c_str(),
                error.c_str());

            success = false;
        }
    }

    return success;
}


bool MQTT_Device::allOff()
{
    if(!isConnected()) {
        return false;
    }

    std::map<std::string, json> payloads;
    for(const auto& [key, binding] : _bindings) {
        (void)key;
        if(binding.safeValue.has_value()) {
            payloads[binding.target][binding.property] = *binding.safeValue;
        }
    }

    if(payloads.empty()) {
        return true;
    }

    bool success = true;
    for(const auto& [target, payload] : payloads) {
        std::string error;
        if(!publishTarget(target, payload, true, error)) {
            reportFailure(_deviceID, error);
            success = false;
        }
    }

    if(success) {
        clearFailure(_deviceID, "MQTT safe values confirmed");
    }

    return success;
}

bool MQTT_Device::deviceAction(std::string command)
{
    command = uppercase(Utils::trim(command));

    if(!_isEnabled) {
        return false;
    }

    if(command == DEVICE_ACTION_ALL_OFF) {
        return allOff();
    }

    LOGT_ERROR("MQTT_Device(%s) unknown DEVICE_ACTION \"%s\"",
               _deviceID.c_str(),
               command.c_str());
    return false;
}

void MQTT_Device::handleMQTTMessage(const std::string& topic,
                                    const json& payload)
{
    if(!payload.is_object()) {
        return;
    }

    const auto topicFound = _topicKeys.find(topic);
    if(topicFound == _topicKeys.end()) {
        return;
    }

    json currentState;
    {
        std::lock_guard<std::mutex> lock(_cacheMutex);

        json& rawState = _rawState[topic];
        if(!rawState.is_object()) {
            rawState = json::object();
        }

        for(const auto& [property, value] : payload.items()) {
            rawState[property] = value;
        }

        currentState = rawState;
    }

    for(const auto& key : topicFound->second) {
        const auto bindingFound = _bindings.find(key);
        if(bindingFound == _bindings.end()) {
            continue;
        }

        const binding_t& binding = bindingFound->second;
        std::optional<std::string> converted;

        if(binding.units == BRIGHTNESS) {
            bool stateKnown = false;
            bool stateOn = false;

            if(currentState.contains(binding.property)) {
                const json& state = currentState[binding.property];

                if(state == binding.onValue) {
                    stateKnown = true;
                    stateOn = true;
                }
                else if(state == binding.offValue) {
                    stateKnown = true;
                    stateOn = false;
                }
                else if(state.is_boolean()) {
                    stateKnown = true;
                    stateOn = state.get<bool>();
                }
                else if(state.is_number_integer()) {
                    const long long numericState = state.get<long long>();
                    if(numericState == 0 || numericState == 1) {
                        stateKnown = true;
                        stateOn = numericState == 1;
                    }
                }
                else if(state.is_string()) {
                    bool parsedState = false;
                    if(stringToBool(state.get<std::string>(), parsedState)) {
                        stateKnown = true;
                        stateOn = parsedState;
                    }
                }
            }

            // Zigbee2MQTT commonly retains the previous brightness while the
            // device is off. The effective brightness is therefore 0 whenever
            // the reported state is off.
            if(stateKnown && !stateOn) {
                converted = "0";
            }
            else if(currentState.contains("brightness") &&
                    currentState["brightness"].is_number()) {
                const double nativeBrightness =
                    currentState["brightness"].get<double>();
                const int percent = static_cast<int>(
                    std::lround(
                        (std::clamp(nativeBrightness, 0.0, 254.0) / 254.0) *
                        100.0));

                // "1" is reserved by keyValueMap_t for Boolean true. Preserve
                // an actual one-percent device report as "1.0".
                if(percent == 1) {
                    converted = "1.0";
                }
                else {
                    converted = std::to_string(percent);
                }
            }
        }
        else {
            if(!currentState.contains(binding.property)) {
                continue;
            }

            converted =
                inboundValue(binding, currentState[binding.property]);
        }

        if(converted.has_value()) {
            cacheValue(key, *converted, true);
            clearFailure(key, "MQTT state received");
        }
    }
}

void MQTT_Device::handleMQTTConnection(bool connected,
                                       const std::string& detail)
{
    if(connected) {
        _deviceState = DEVICE_STATE_CONNECTED;
        clearFailure(_deviceID, "MQTT broker connected");
        return;
    }

    _deviceState = DEVICE_STATE_DISCONNECTED;
    if(!detail.empty()) {
        reportFailure(_deviceID, detail);
    }
}

void MQTT_Device::cacheValue(const std::string& key,
                             const std::string& value,
                             bool markPending)
{
    std::lock_guard<std::mutex> lock(_cacheMutex);

    const auto found = _cachedValues.find(key);
    const bool changed = found == _cachedValues.end() || found->second != value;
    _cachedValues[key] = value;

    if(markPending && changed) {
        _pendingValues[key] = value;
    }
}

MQTT_Device::json MQTT_Device::outboundValue(const binding_t& binding,
                                             const std::string& value)
{
    const std::string trimmed = Utils::trim(value);

    if(binding.units == BOOL) {
        bool state = false;
        if(!stringToBool(trimmed, state)) {
            throw std::runtime_error(binding.key + " requires a BOOL value");
        }
        return state ? binding.onValue : binding.offValue;
    }

    if(isInovelliColorProperty(binding.property)) {
        return rgbToInovelliColor(trimmed);
    }

    if(binding.units == PERCENT) {
        std::size_t consumed = 0;
        const double number = std::stod(trimmed, &consumed);
        if(consumed != trimmed.size() || number < 0.0 || number > 100.0) {
            throw std::runtime_error(binding.key + " requires a percentage from 0 to 100");
        }

        if(binding.property == "brightness") {
            return static_cast<int>(std::lround((number / 100.0) * 254.0));
        }

        return number;
    }

    if(isNumericUnit(binding.units)) {
        if(isIntegerUnit(binding.units)) {
            std::size_t consumed = 0;
            const long long number = std::stoll(trimmed, &consumed, 10);
            if(consumed != trimmed.size()) {
                throw std::runtime_error(binding.key + " requires an integer value");
            }
            return number;
        }

        std::size_t consumed = 0;
        const double number = std::stod(trimmed, &consumed);
        if(consumed != trimmed.size()) {
            throw std::runtime_error(binding.key + " requires a numeric value");
        }
        return number;
    }

    return trimmed;
}

std::optional<std::string> MQTT_Device::inboundValue(const binding_t& binding,
                                                     const json& value)
{
    if(binding.units == BOOL) {
        if(value == binding.onValue) {
            return "1";
        }
        if(value == binding.offValue) {
            return "0";
        }
        if(value.is_boolean()) {
            return value.get<bool>() ? "1" : "0";
        }
        if(value.is_number_integer()) {
            const long long number = value.get<long long>();
            if(number == 0 || number == 1) {
                return number == 1 ? "1" : "0";
            }
        }
        if(value.is_string()) {
            bool state = false;
            if(stringToBool(value.get<std::string>(), state)) {
                return state ? "1" : "0";
            }
        }
        return std::nullopt;
    }

    if(isInovelliColorProperty(binding.property)) {
        if(value.is_number_integer()) {
            return inovelliColorToHex(value.get<int>());
        }
        if(value.is_string()) {
            return value.get<std::string>();
        }
        return std::nullopt;
    }

    if(binding.units == PERCENT && binding.property == "brightness") {
        if(!value.is_number()) {
            return std::nullopt;
        }

        const double native = value.get<double>();
        const int percent = static_cast<int>(
            std::lround((std::clamp(native, 0.0, 254.0) / 254.0) * 100.0));
        return std::to_string(percent);
    }

    if(value.is_string()) {
        return value.get<std::string>();
    }
    if(value.is_boolean()) {
        return value.get<bool>() ? "1" : "0";
    }
    if(value.is_number_integer()) {
        return std::to_string(value.get<long long>());
    }
    if(value.is_number_unsigned()) {
        return std::to_string(value.get<unsigned long long>());
    }
    if(value.is_number_float()) {
        return compactNumber(value.get<double>());
    }

    return value.dump();
}

bool MQTT_Device::stateMatches(const json& reported, const json& requested)
{
    if(!reported.is_object() || !requested.is_object()) {
        return false;
    }

    for(const auto& [property, expected] : requested.items()) {
        if(!reported.contains(property)) {
            return false;
        }

        const json& actual = reported[property];
        if(actual == expected) {
            continue;
        }

        if(actual.is_string() && expected.is_string() &&
           uppercase(actual.get<std::string>()) ==
               uppercase(expected.get<std::string>())) {
            continue;
        }

        if(actual.is_number() && expected.is_number() &&
           std::fabs(actual.get<double>() - expected.get<double>()) < 0.51) {
            continue;
        }

        return false;
    }

    return true;
}

bool MQTT_Device::isInovelliColorProperty(const std::string& property)
{
    return property == "ledColorWhenOn" || property == "ledColorWhenOff";
}

int MQTT_Device::rgbToInovelliColor(const std::string& color)
{
    const std::string trimmed = Utils::trim(color);
    if(isIntegerString(trimmed)) {
        const int native = std::stoi(trimmed);
        if(native >= 0 && native <= 255) {
            return native;
        }
    }

    const rgb_t rgb = parseColor(trimmed);
    if(rgb.red == 0 && rgb.green == 0 && rgb.blue == 0) {
        throw std::runtime_error(
            "black requires the matching Inovelli LED intensity to be set to 0");
    }
    if(rgb.red == 255 && rgb.green == 255 && rgb.blue == 255) {
        return 255;
    }

    const double red = static_cast<double>(rgb.red) / 255.0;
    const double green = static_cast<double>(rgb.green) / 255.0;
    const double blue = static_cast<double>(rgb.blue) / 255.0;
    const double maximum = std::max({red, green, blue});
    const double minimum = std::min({red, green, blue});
    const double delta = maximum - minimum;

    if(delta == 0.0) {
        return 255;
    }

    double hue = 0.0;
    if(maximum == red) {
        hue = 60.0 * std::fmod((green - blue) / delta, 6.0);
    }
    else if(maximum == green) {
        hue = 60.0 * (((blue - red) / delta) + 2.0);
    }
    else {
        hue = 60.0 * (((red - green) / delta) + 4.0);
    }

    if(hue < 0.0) {
        hue += 360.0;
    }

    return std::clamp(
        static_cast<int>(std::lround((hue / 360.0) * 254.0)),
        0,
        254);
}

std::string MQTT_Device::inovelliColorToHex(int nativeColor)
{
    if(nativeColor == 255) {
        return "#FFFFFF";
    }

    const double hue =
        (static_cast<double>(std::clamp(nativeColor, 0, 254)) / 254.0) * 360.0;
    const double chroma = 1.0;
    const double x = chroma *
        (1.0 - std::fabs(std::fmod(hue / 60.0, 2.0) - 1.0));

    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;

    if(hue < 60.0) {
        red = chroma;
        green = x;
    }
    else if(hue < 120.0) {
        red = x;
        green = chroma;
    }
    else if(hue < 180.0) {
        green = chroma;
        blue = x;
    }
    else if(hue < 240.0) {
        green = x;
        blue = chroma;
    }
    else if(hue < 300.0) {
        red = x;
        blue = chroma;
    }
    else {
        red = chroma;
        blue = x;
    }

    std::ostringstream output;
    output << '#'
           << std::uppercase
           << std::hex
           << std::setfill('0')
           << std::setw(2) << static_cast<int>(std::lround(red * 255.0))
           << std::setw(2) << static_cast<int>(std::lround(green * 255.0))
           << std::setw(2) << static_cast<int>(std::lround(blue * 255.0));
    return output.str();
}

void MQTT_Device::reportFailure(const std::string& key,
                                const std::string& detail)
{
    IncidentMgr::shared()->raise(
        _deviceID,
        IncidentMgr::Severity::Error,
        "DEVICE_IO_FAILED",
        key,
        nullptr,
        detail.c_str());
}

void MQTT_Device::clearFailure(const std::string& key,
                               const std::string& detail)
{
    IncidentMgr::shared()->clear(
        _deviceID,
        "DEVICE_IO_FAILED",
        key,
        nullptr,
        detail.c_str());
}
