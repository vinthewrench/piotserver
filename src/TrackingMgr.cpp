//
// TrackingMgr.cpp
//

#include "TrackingMgr.hpp"
#include "pIoTServerDB.hpp"
#include "LogMgr.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <ctime>

using namespace nlohmann;
using namespace std;

TrackingMgr* TrackingMgr::_sharedInstance = nullptr;

TrackingMgr* TrackingMgr::shared()
{
    if(!_sharedInstance) {
        _sharedInstance = new TrackingMgr;
    }

    return _sharedInstance;
}

TrackingMgr::TrackingMgr()
{
}

bool TrackingMgr::begin(pIoTServerDB* db)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if(!db) {
        LOG_ERROR("TrackingMgr begin failed: db is null");
        return false;
    }

    _db = db;

    if(!_db->initTrackingTables()) {
        LOG_ERROR("TrackingMgr begin failed: initTrackingTables failed");
        _db = nullptr;
        _isSetup = false;
        return false;
    }

    _itemsByKey.clear();
    _actionEffects.clear();

    _isSetup = true;
    return true;
}


void TrackingMgr::stop()
{
    std::lock_guard<std::mutex> lock(_mutex);

    _itemsByKey.clear();
    _actionEffects.clear();

    _db = nullptr;
    _isSetup = false;
}

bool TrackingMgr::configure(const nlohmann::json& config)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if(!_isSetup || !_db) {
        LOG_ERROR("TrackingMgr configure failed: manager is not setup");
        return false;
    }

    _itemsByKey.clear();
    _actionEffects.clear();

    return loadConfig(config);
}

bool TrackingMgr::isSetup() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _isSetup;
}

bool TrackingMgr::setValues(const std::map<std::string, std::string>& kv)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if(!_isSetup || !_db) {
        return false;
    }

    bool handled = false;

    for(const auto& [key, value] : kv) {
        if(handleValue(key, value)) {
            handled = true;
        }
    }

    return handled;
}

bool TrackingMgr::deviceAction(const std::string& deviceID,
                               const std::string& action,
                               bool success)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if(!_isSetup || !_db) {
        return false;
    }

    if(!success) {
        return false;
    }

    bool handled = false;

    for(const auto& effect : _actionEffects) {
        if(effect.deviceID != deviceID) {
            continue;
        }

        if(effect.action != action) {
            continue;
        }

        switch(effect.effect) {
            case ActionEffect::Inactive:
                if(forceInactiveForDevice(deviceID)) {
                    handled = true;
                }
                break;

            case ActionEffect::Unknown:
            default:
                break;
        }
    }

    return handled;
}

bool TrackingMgr::isTrackingKey(const std::string& key) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _itemsByKey.count(key) != 0;
}

bool TrackingMgr::loadConfig(const nlohmann::json& config)
{
    if(config.is_null()) {
        return true;
    }

    if(!config.is_object()) {
        LOG_ERROR("TrackingMgr config is not an object");
        return false;
    }

    bool enabled = true;

    if(config.contains("enabled") && config["enabled"].is_boolean()) {
        enabled = config["enabled"].get<bool>();
    }

    if(!enabled) {
        LOG_INFO("TrackingMgr disabled by config");
        return true;
    }

    if(config.contains("items") && config["items"].is_array()) {
        for(const auto& entry : config["items"]) {
            if(!entry.is_object()) {
                continue;
            }

            if(!entry.contains("key") || !entry["key"].is_string()) {
                LOG_ERROR("TrackingMgr item missing key");
                continue;
            }

            trackingItem_t item = {};

            item.key = entry["key"].get<std::string>();
            std::transform(item.key.begin(), item.key.end(), item.key.begin(), ::toupper);

            if(entry.contains("device") && entry["device"].is_string()) {
                item.deviceID = entry["device"].get<std::string>();
            }
            else if(entry.contains("deviceID") && entry["deviceID"].is_string()) {
                item.deviceID = entry["deviceID"].get<std::string>();
            }

            if(entry.contains("kind") && entry["kind"].is_string()) {
                item.kind = kindForString(entry["kind"].get<std::string>());
            }
            else {
                item.kind = Kind::Duration;
            }

            if(item.kind == Kind::Unknown) {
                LOGT_ERROR("TrackingMgr item %s has unknown kind", item.key.c_str());
                continue;
            }

            if(entry.contains("active_value")) {
                item.activeValue = normalizeValue(entry["active_value"]);
            }
            else {
                item.activeValue = "1";
            }

            item.active = false;
            item.startTime = 0;

            _itemsByKey[item.key] = item;

            LOGT_INFO("TrackingMgr tracking key %s kind=%d active_value=%s device=%s",
                      item.key.c_str(),
                      (int)item.kind,
                      item.activeValue.c_str(),
                      item.deviceID.c_str());
        }
    }

    if(config.contains("action_effects") && config["action_effects"].is_array()) {
        for(const auto& entry : config["action_effects"]) {
            if(!entry.is_object()) {
                continue;
            }

            trackingActionEffect_t effect = {};

            if(entry.contains("device") && entry["device"].is_string()) {
                effect.deviceID = entry["device"].get<std::string>();
            }
            else if(entry.contains("deviceID") && entry["deviceID"].is_string()) {
                effect.deviceID = entry["deviceID"].get<std::string>();
            }

            if(entry.contains("action") && entry["action"].is_string()) {
                effect.action = entry["action"].get<std::string>();
            }

            if(entry.contains("effect") && entry["effect"].is_string()) {
                effect.effect = actionEffectForString(entry["effect"].get<std::string>());
            }

            if(effect.deviceID.empty() || effect.action.empty() || effect.effect == ActionEffect::Unknown) {
                LOG_ERROR("TrackingMgr action_effect malformed");
                continue;
            }

            _actionEffects.push_back(effect);

            LOGT_INFO("TrackingMgr action effect device=%s action=%s effect=%d",
                      effect.deviceID.c_str(),
                      effect.action.c_str(),
                      (int)effect.effect);
        }
    }

    return true;
}

bool TrackingMgr::handleValue(const std::string& key,
                              const std::string& value)
{
    auto it = _itemsByKey.find(key);

    if(it == _itemsByKey.end()) {
        return false;
    }

    trackingItem_t& item = it->second;

    if(item.kind != Kind::Duration) {
        return false;
    }

    std::string normalizedValue = value;

    bool boolState = false;
    if(stringToBool(value, boolState)) {
        normalizedValue = boolState ? "1" : "0";
    }

    bool shouldBeActive = (normalizedValue == item.activeValue);

    if(shouldBeActive) {
        if(!item.active) {
            item.active = true;
            item.startTime = time(nullptr);

            LOGT_INFO("TrackingMgr start %s at %ld",
                      item.key.c_str(),
                      (long)item.startTime);
        }

        return true;
    }

    if(item.active) {
        return closeDurationItem(item);
    }

    return true;
}

bool TrackingMgr::forceInactiveForKey(const std::string& key)
{
    auto it = _itemsByKey.find(key);

    if(it == _itemsByKey.end()) {
        return false;
    }

    trackingItem_t& item = it->second;

    if(item.kind != Kind::Duration) {
        return false;
    }

    if(item.active) {
        return closeDurationItem(item);
    }

    return true;
}

bool TrackingMgr::forceInactiveForDevice(const std::string& deviceID)
{
    bool handled = false;

    for(auto& [key, item] : _itemsByKey) {
        if(item.deviceID != deviceID) {
            continue;
        }

        if(item.kind != Kind::Duration) {
            continue;
        }

        if(item.active) {
            if(closeDurationItem(item)) {
                handled = true;
            }
        }
    }

    return handled;
}

bool TrackingMgr::closeDurationItem(trackingItem_t& item)
{
    if(!_db) {
        return false;
    }

    if(!item.active || item.startTime <= 0) {
        item.active = false;
        item.startTime = 0;
        return false;
    }

    time_t now = time(nullptr);

    if(now <= item.startTime) {
        item.active = false;
        item.startTime = 0;
        return false;
    }

    uint32_t durationSec = static_cast<uint32_t>(now - item.startTime);
    time_t startTime = item.startTime;

    bool success = _db->insertTrackingDuration(item.key,
                                               startTime,
                                               durationSec);

    if(success) {
        LOGT_INFO("TrackingMgr recorded %s start=%ld duration=%u",
                  item.key.c_str(),
                  (long)startTime,
                  durationSec);
    }
    else {
        LOGT_ERROR("TrackingMgr failed to record %s start=%ld duration=%u",
                   item.key.c_str(),
                   (long)startTime,
                   durationSec);
    }

    item.active = false;
    item.startTime = 0;

    return success;
}

TrackingMgr::Kind TrackingMgr::kindForString(const std::string& str)
{
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);

    if(s == "duration") {
        return Kind::Duration;
    }

    return Kind::Unknown;
}

TrackingMgr::ActionEffect TrackingMgr::actionEffectForString(const std::string& str)
{
    std::string s = str;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);

    if(s == "inactive") {
        return ActionEffect::Inactive;
    }

    if(s == "set_inactive") {
        return ActionEffect::Inactive;
    }

    return ActionEffect::Unknown;
}

std::string TrackingMgr::normalizeValue(const nlohmann::json& value)
{
    if(value.is_boolean()) {
        return value.get<bool>() ? "1" : "0";
    }

    if(value.is_number_integer()) {
        return value.get<int>() ? "1" : "0";
    }

    if(value.is_number_unsigned()) {
        return value.get<unsigned int>() ? "1" : "0";
    }

    if(value.is_string()) {
        std::string s = value.get<std::string>();

        bool boolState = false;
        if(stringToBool(s, boolState)) {
            return boolState ? "1" : "0";
        }

        return s;
    }

    return "1";
}
