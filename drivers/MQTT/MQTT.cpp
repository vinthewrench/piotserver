//
//  MQTT.cpp
//  pIoTServer MQTT transport
//

#include "MQTT.hpp"
#include "LogMgr.hpp"

#include <mosquitto.h>

#include <limits>
#include <utility>

std::mutex MQTT::_libraryMutex;
unsigned int MQTT::_libraryUsers = 0;

MQTT::MQTT() = default;

MQTT::~MQTT()
{
    stop();
}

bool MQTT::acquireLibrary(std::string& errorOut)
{
    std::lock_guard<std::mutex> lock(_libraryMutex);

    if(_libraryUsers == 0) {
        const int result = mosquitto_lib_init();
        if(result != MOSQ_ERR_SUCCESS) {
            errorOut = std::string("mosquitto_lib_init failed: ") +
                       mosquitto_strerror(result);
            return false;
        }
    }

    ++_libraryUsers;
    return true;
}

void MQTT::releaseLibrary()
{
    std::lock_guard<std::mutex> lock(_libraryMutex);

    if(_libraryUsers == 0) {
        return;
    }

    --_libraryUsers;
    if(_libraryUsers == 0) {
        mosquitto_lib_cleanup();
    }
}

bool MQTT::begin(const config_t& config,
                 messageCallback_t messageCallback,
                 connectionCallback_t connectionCallback,
                 std::string& errorOut)
{
    stop();
    errorOut.clear();

    if(config.host.empty()) {
        errorOut = "MQTT host is empty";
        return false;
    }
    if(config.port < 1 || config.port > 65535) {
        errorOut = "MQTT port is invalid";
        return false;
    }
    if(config.keepAlive < 1) {
        errorOut = "MQTT keepalive must be positive";
        return false;
    }
    if(config.qos < 0 || config.qos > 2) {
        errorOut = "MQTT QoS must be 0, 1, or 2";
        return false;
    }
    if(config.timeout.count() < 1) {
        errorOut = "MQTT timeout must be positive";
        return false;
    }
    if(config.username.empty() && !config.password.empty()) {
        errorOut = "MQTT password requires a username";
        return false;
    }

    if(!acquireLibrary(errorOut)) {
        return false;
    }
    _libraryAcquired = true;

    _config = config;
    _messageCallback = std::move(messageCallback);
    _connectionCallback = std::move(connectionCallback);
    _stopping = false;
    _connected = false;
    _connectionError.clear();

    const char* clientID = _config.clientID.empty()
        ? nullptr
        : _config.clientID.c_str();

    _client = mosquitto_new(clientID, true, this);
    if(_client == nullptr) {
        errorOut = "mosquitto_new failed";
        stop();
        return false;
    }

    mosquitto_connect_callback_set(_client, &MQTT::connectCallback);
    mosquitto_disconnect_callback_set(_client, &MQTT::disconnectCallback);
    mosquitto_message_callback_set(_client, &MQTT::messageCallback);
    mosquitto_publish_callback_set(_client, &MQTT::publishCallback);
    mosquitto_reconnect_delay_set(_client, 1, 10, true);

    if(!_config.username.empty()) {
        const char* password = _config.password.empty()
            ? nullptr
            : _config.password.c_str();
        const int result = mosquitto_username_pw_set(
            _client,
            _config.username.c_str(),
            password);

        if(result != MOSQ_ERR_SUCCESS) {
            errorOut = std::string("MQTT username/password setup failed: ") +
                       mosquitto_strerror(result);
            stop();
            return false;
        }
    }

    int result = mosquitto_connect_async(
        _client,
        _config.host.c_str(),
        _config.port,
        _config.keepAlive);

    if(result != MOSQ_ERR_SUCCESS) {
        errorOut = std::string("MQTT connect failed: ") +
                   mosquitto_strerror(result);
        stop();
        return false;
    }

    result = mosquitto_loop_start(_client);
    if(result != MOSQ_ERR_SUCCESS) {
        errorOut = std::string("MQTT network loop failed: ") +
                   mosquitto_strerror(result);
        stop();
        return false;
    }
    _loopStarted = true;

    {
        std::unique_lock<std::mutex> lock(_mutex);
        const bool completed = _condition.wait_for(
            lock,
            _config.timeout,
            [this]() {
                return _connected || !_connectionError.empty();
            });

        if(!completed) {
            errorOut = "Timed out connecting to MQTT broker";
        }
        else if(!_connected) {
            errorOut = _connectionError.empty()
                ? "MQTT connection failed"
                : _connectionError;
        }
    }

    if(!errorOut.empty()) {
        stop();
        return false;
    }

    return true;
}

void MQTT::stop()
{
    mosquitto* client = nullptr;
    bool loopStarted = false;

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _stopping = true;
        client = _client;
        loopStarted = _loopStarted;
    }

    if(client != nullptr) {
        if(loopStarted) {
            mosquitto_disconnect(client);
            mosquitto_loop_stop(client, true);
        }
        mosquitto_destroy(client);
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _client = nullptr;
        _loopStarted = false;
        _connected = false;
        _connectionError.clear();
        _subscriptions.clear();
        _acknowledgedPublishes.clear();
        _messages.clear();
        _nextGeneration = 1;
        _messageCallback = nullptr;
        _connectionCallback = nullptr;
        _condition.notify_all();
    }

    if(_libraryAcquired) {
        _libraryAcquired = false;
        releaseLibrary();
    }
}

bool MQTT::isConnected() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _connected;
}

bool MQTT::resubscribe(const std::string& topic)
{
    if(_client == nullptr) {
        return false;
    }

    int messageID = 0;
    const int result = mosquitto_subscribe(
        _client,
        &messageID,
        topic.c_str(),
        _config.qos);

    return result == MOSQ_ERR_SUCCESS;
}

bool MQTT::subscribe(const std::string& topic, std::string& errorOut)
{
    errorOut.clear();

    if(topic.empty()) {
        errorOut = "MQTT subscription topic is empty";
        LOGT_ERROR("%s", errorOut.c_str());
        return false;
    }

    bool connected = false;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _subscriptions.insert(topic);
        connected = _connected;
    }

    if(connected && !resubscribe(topic)) {
        errorOut = "Unable to subscribe to MQTT topic " + topic;
        LOGT_ERROR("%s", errorOut.c_str());
        return false;
    }

    return true;
}

bool MQTT::publish(const std::string& topic,
                   const json& payload,
                   std::string& errorOut)
{
    errorOut.clear();

    if(topic.empty()) {
        errorOut = "MQTT publish topic is empty";
        LOGT_ERROR("%s", errorOut.c_str());
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(_mutex);
        if(!_connected || _client == nullptr) {
            errorOut = _connectionError.empty()
                ? "MQTT broker is not connected"
                : _connectionError;

            LOGT_ERROR("MQTT publish topic=%s failed: %s",
                       topic.c_str(),
                       errorOut.c_str());
            return false;
        }
    }

    const std::string body = payload.dump();
    if(body.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        errorOut = "MQTT payload is too large";
        LOGT_ERROR("MQTT publish topic=%s failed: %s",
                   topic.c_str(),
                   errorOut.c_str());
        return false;
    }

    int messageID = 0;
    const int result = mosquitto_publish(
        _client,
        &messageID,
        topic.c_str(),
        static_cast<int>(body.size()),
        body.data(),
        _config.qos,
        false);

    if(result != MOSQ_ERR_SUCCESS) {
        errorOut = std::string("MQTT publish failed for ") + topic + ": " +
                   mosquitto_strerror(result);

        LOGT_ERROR("%s", errorOut.c_str());
        return false;
    }

    if(_config.qos == 0) {
        return true;
    }

    std::unique_lock<std::mutex> lock(_mutex);
    const bool completed = _condition.wait_for(
        lock,
        _config.timeout,
        [this, messageID]() {
            return _acknowledgedPublishes.count(messageID) > 0 || !_connected;
        });

    if(!completed) {
        errorOut = "Timed out publishing to MQTT topic " + topic;
        LOGT_ERROR("%s", errorOut.c_str());
        return false;
    }

    if(_acknowledgedPublishes.erase(messageID) == 0) {
        errorOut = _connectionError.empty()
            ? "MQTT connection was lost while publishing"
            : _connectionError;

        LOGT_ERROR("MQTT publish topic=%s failed: %s",
                   topic.c_str(),
                   errorOut.c_str());
        return false;
    }

    return true;
}

std::uint64_t MQTT::generationForTopic(const std::string& topic) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    const auto found = _messages.find(topic);
    return found == _messages.end() ? 0 : found->second.generation;
}

bool MQTT::waitForMessage(const std::string& topic,
                          std::uint64_t afterGeneration,
                          const messagePredicate_t& predicate,
                          json& messageOut,
                          std::string& errorOut)
{
    errorOut.clear();

    std::unique_lock<std::mutex> lock(_mutex);
    const bool completed = _condition.wait_for(
        lock,
        _config.timeout,
        [this, &topic, afterGeneration, &predicate]() {
            const auto found = _messages.find(topic);
            if(found != _messages.end() &&
               found->second.generation > afterGeneration &&
               (!predicate || predicate(found->second.payload))) {
                return true;
            }

            return !_connected;
        });

    if(!completed) {
        errorOut = "Timed out waiting for MQTT state on " + topic;
        return false;
    }

    const auto found = _messages.find(topic);
    if(found == _messages.end() ||
       found->second.generation <= afterGeneration ||
       (predicate && !predicate(found->second.payload))) {
        errorOut = _connectionError.empty()
            ? "MQTT connection was lost while waiting for state"
            : _connectionError;
        return false;
    }

    messageOut = found->second.payload;
    return true;
}

void MQTT::connectCallback(mosquitto*, void* object, int result)
{
    if(object != nullptr) {
        static_cast<MQTT*>(object)->onConnect(result);
    }
}

void MQTT::disconnectCallback(mosquitto*, void* object, int result)
{
    if(object != nullptr) {
        static_cast<MQTT*>(object)->onDisconnect(result);
    }
}

void MQTT::messageCallback(mosquitto*,
                           void* object,
                           const mosquitto_message* message)
{
    if(object != nullptr) {
        static_cast<MQTT*>(object)->onMessage(message);
    }
}

void MQTT::publishCallback(mosquitto*, void* object, int messageID)
{
    if(object != nullptr) {
        static_cast<MQTT*>(object)->onPublish(messageID);
    }
}

void MQTT::onConnect(int result)
{
    std::set<std::string> subscriptions;
    connectionCallback_t callback;
    std::string status;

    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(result == 0) {
            _connected = true;
            _connectionError.clear();
            subscriptions = _subscriptions;

            LOGT_INFO("MQTT connected to broker %s:%d",
                      _config.host.c_str(),
                      _config.port);
        }
        else {
            _connected = false;
            _connectionError = std::string("MQTT connection rejected: ") +
                               mosquitto_connack_string(result);
            status = _connectionError;

            LOGT_ERROR("MQTT connection to %s:%d rejected: %s",
                       _config.host.c_str(),
                       _config.port,
                       status.c_str());
        }

        callback = _connectionCallback;
        _condition.notify_all();
    }

    if(result == 0) {
        for(const auto& topic : subscriptions) {
            if(!resubscribe(topic)) {
                LOGT_ERROR("MQTT failed to subscribe topic=%s", topic.c_str());
            }
        }
    }

    if(callback) {
        callback(result == 0, status);
    }
}

void MQTT::onDisconnect(int result)
{
    connectionCallback_t callback;
    std::string status;
    bool shouldNotify = false;

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _connected = false;

        if(!_stopping && result != 0) {
            _connectionError = std::string("MQTT connection lost: ") +
                               mosquitto_strerror(result);
            status = _connectionError;
            shouldNotify = true;

            LOGT_ERROR("MQTT disconnected unexpectedly: %s",
                       status.c_str());
        }

        callback = _connectionCallback;
        _condition.notify_all();
    }

    if(shouldNotify && callback) {
        callback(false, status);
    }
}

void MQTT::onMessage(const mosquitto_message* message)
{
    if(message == nullptr || message->topic == nullptr || message->payloadlen < 0) {
        LOGT_ERROR("MQTT received an invalid message");
        return;
    }

    const std::string body = message->payload == nullptr
        ? std::string()
        : std::string(static_cast<const char*>(message->payload),
                      static_cast<std::size_t>(message->payloadlen));

    json payload = json::parse(body, nullptr, false);
    if(payload.is_discarded()) {
        LOGT_ERROR("MQTT received invalid JSON on topic %s", message->topic);
        payload = body;
    }

    messageCallback_t callback;
    const std::string topic = message->topic;

    {
        std::lock_guard<std::mutex> lock(_mutex);
        _messages[topic] = {payload, _nextGeneration++};
        callback = _messageCallback;
        _condition.notify_all();
    }

    if(callback) {
        callback(topic, payload);
    }
}

void MQTT::onPublish(int messageID)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _acknowledgedPublishes.insert(messageID);
    _condition.notify_all();
}
