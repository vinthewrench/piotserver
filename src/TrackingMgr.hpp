//
// TrackingMgr.hpp
//

#pragma once

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <time.h>

#include "json.hpp"

class pIoTServerDB;

class TrackingMgr {
public:
    enum class Kind {
        Unknown  = 0,
        Duration = 1
    };

    enum class ActionEffect {
        Unknown  = 0,
        Inactive = 1
    };

    typedef struct trackingItem {
        std::string key;
        std::string deviceID;
        Kind kind = Kind::Unknown;
        std::string activeValue = "1";

        bool active = false;
        time_t startTime = 0;
    } trackingItem_t;

    typedef struct trackingActionEffect {
        std::string deviceID;
        std::string action;
        ActionEffect effect = ActionEffect::Unknown;
    } trackingActionEffect_t;

    static TrackingMgr* shared();

    bool begin(pIoTServerDB* db);
    bool configure(const nlohmann::json& config);
    nlohmann::json jsonConfig() const;
    void stop();

    bool isSetup() const;

    bool setValues(const std::map<std::string, std::string>& kv);

    bool deviceAction(const std::string& deviceID,
                      const std::string& action,
                      bool success);

    bool isTrackingKey(const std::string& key) const;

    std::string printString() const;
    void dumpTracking() const;

private:
    TrackingMgr();

    static TrackingMgr* _sharedInstance;

    bool loadConfig(const nlohmann::json& config);

    bool handleValue(const std::string& key,
                     const std::string& value);

    bool forceInactiveForKey(const std::string& key);

    bool forceInactiveForDevice(const std::string& deviceID);

    bool closeDurationItem(trackingItem_t& item);

    static Kind kindForString(const std::string& str);
    static ActionEffect actionEffectForString(const std::string& str);
    static std::string normalizeValue(const nlohmann::json& value);

    static std::string stringForKind(Kind kind);
    static std::string stringForActionEffect(ActionEffect effect);
    static std::string normalizeDeviceID(const std::string& str);

    mutable std::mutex _mutex;
    pIoTServerDB* _db = nullptr;
    bool _isSetup = false;

    std::map<std::string, trackingItem_t> _itemsByKey;
    std::vector<trackingActionEffect_t> _actionEffects;
};
