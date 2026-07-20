//
//  MQTT.hpp
//  pIoTServer MQTT transport
//

#ifndef PIOTSERVER_MQTT_HPP
#define PIOTSERVER_MQTT_HPP

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>

#include "json.hpp"

struct mosquitto;
struct mosquitto_message;

class MQTT {
public:
    using json = nlohmann::json;
    using messageCallback_t = std::function<void(const std::string&, const json&)>;
    using connectionCallback_t = std::function<void(bool, const std::string&)>;
    using messagePredicate_t = std::function<bool(const json&)>;

    struct config_t {
        std::string host;
        int port = 1883;
        std::string username;
        std::string password;
        std::string clientID;
        int keepAlive = 30;
        int qos = 1;
        std::chrono::milliseconds timeout{3000};
    };

    MQTT();
    ~MQTT();

    MQTT(const MQTT&) = delete;
    MQTT& operator=(const MQTT&) = delete;

    bool begin(const config_t& config,
               messageCallback_t messageCallback,
               connectionCallback_t connectionCallback,
               std::string& errorOut);
    void stop();

    bool isConnected() const;

    bool subscribe(const std::string& topic, std::string& errorOut);
    bool publish(const std::string& topic,
                 const json& payload,
                 std::string& errorOut);

    std::uint64_t generationForTopic(const std::string& topic) const;
    bool waitForMessage(const std::string& topic,
                        std::uint64_t afterGeneration,
                        const messagePredicate_t& predicate,
                        json& messageOut,
                        std::string& errorOut);

private:
    struct storedMessage_t {
        json payload;
        std::uint64_t generation = 0;
    };

    static std::mutex _libraryMutex;
    static unsigned int _libraryUsers;

    static bool acquireLibrary(std::string& errorOut);
    static void releaseLibrary();

    static void connectCallback(mosquitto* client, void* object, int result);
    static void disconnectCallback(mosquitto* client, void* object, int result);
    static void messageCallback(mosquitto* client,
                                void* object,
                                const mosquitto_message* message);
    static void publishCallback(mosquitto* client, void* object, int messageID);

    void onConnect(int result);
    void onDisconnect(int result);
    void onMessage(const mosquitto_message* message);
    void onPublish(int messageID);

    bool resubscribe(const std::string& topic);

    config_t _config;
    mosquitto* _client = nullptr;
    bool _libraryAcquired = false;
    bool _loopStarted = false;
    bool _connected = false;
    bool _stopping = false;
    std::string _connectionError;

    messageCallback_t _messageCallback;
    connectionCallback_t _connectionCallback;

    mutable std::mutex _mutex;
    std::condition_variable _condition;
    std::set<std::string> _subscriptions;
    std::set<int> _acknowledgedPublishes;
    std::map<std::string, storedMessage_t> _messages;
    std::uint64_t _nextGeneration = 1;
};

#endif // PIOTSERVER_MQTT_HPP
