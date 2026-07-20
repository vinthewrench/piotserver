//
//  MQTT_Device.hpp
//  pIoTServer MQTT plugin device
//

#ifndef PIOTSERVER_MQTT_DEVICE_HPP
#define PIOTSERVER_MQTT_DEVICE_HPP

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "MQTT.hpp"
#include "pIoTServerDevice.hpp"

class MQTT_Device : public pIoTServerDevice {
public:
    static constexpr std::string_view DEVICE_ACTION_ALL_OFF = "ALL_OFF";

    MQTT_Device(std::string devID, std::string driverName);
    explicit MQTT_Device(std::string devID);
    ~MQTT_Device() override;

    bool getVersion(std::string& version) override;
    void getProperties(nlohmann::json& properties) override;
    bool initWithSchema(deviceSchemaMap_t deviceSchema) override;

    bool start() override;
    void stop() override;
    bool isConnected() override;
    bool setEnabled(bool enable) override;

    bool hasUpdates() override;
    bool getValues(keyValueMap_t& results) override;
    bool setValues(keyValueMap_t values) override;

    bool allOff() override;
    bool deviceAction(std::string command) override;

private:
    using json = nlohmann::json;

    struct binding_t {
        std::string key;
        std::string target;
        std::string targetType = "device";
        std::string property;
        valueSchemaUnits_t units = UNKNOWN;
        bool readOnly = true;
        json onValue = "ON";
        json offValue = "OFF";
        std::optional<json> safeValue;
    };

    bool parseDeviceConfig(MQTT::config_t& configOut,
                           std::string& errorOut);
    bool publishTarget(const std::string& target,
                       const json& payload,
                       bool waitForConfirmation,
                       std::string& errorOut);
    void requestInitialState();

    std::string stateTopic(const std::string& target) const;
    std::string setTopic(const std::string& target) const;
    std::string getTopic(const std::string& target) const;

    void handleMQTTMessage(const std::string& topic, const json& payload);
    void handleMQTTConnection(bool connected, const std::string& detail);
    void cacheValue(const std::string& key,
                    const std::string& value,
                    bool markPending);

    static json outboundValue(const binding_t& binding,
                              const std::string& value);
    static std::optional<std::string> inboundValue(const binding_t& binding,
                                                   const json& value);
    static bool stateMatches(const json& reported, const json& requested);
    static int rgbToInovelliColor(const std::string& color);
    static std::string inovelliColorToHex(int nativeColor);
    static bool isInovelliColorProperty(const std::string& property);

    void reportFailure(const std::string& key, const std::string& detail);
    void clearFailure(const std::string& key, const std::string& detail);

    std::map<std::string, binding_t> _bindings;
    std::map<std::string, std::vector<std::string>> _topicKeys;

    std::unique_ptr<MQTT> _mqtt;
    std::string _baseTopic = "zigbee2mqtt";
    bool _isSetup = false;

    mutable std::mutex _cacheMutex;
    keyValueMap_t _cachedValues;
    keyValueMap_t _pendingValues;
    std::map<std::string, json> _rawState;
};

#endif // PIOTSERVER_MQTT_DEVICE_HPP
