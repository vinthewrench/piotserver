//
// TrackingMgr.cpp
//

#include "TrackingMgr.hpp"
#include "pIoTServerDB.hpp"
#include "LogMgr.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <ctime>
#include <cstdio>

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

    /*
     * Do not clear _itemsByKey or _actionEffects here.
     *
     * Tracking config may have already been loaded by
     * restorePropertiesFromFile() before the database is opened.
     *
     * configure() owns replacing tracking config.
     * stop() owns clearing runtime/config state.
     */

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

    _itemsByKey.clear();
    _actionEffects.clear();

    return loadConfig(config);
}

nlohmann::json TrackingMgr::jsonConfig() const
{
    std::lock_guard<std::mutex> lock(_mutex);

    json config;

    config["enabled"] = true;

    /*
     * _itemsByKey is flat internally, but props are easier to read when
     * equivalent items are grouped into one entry with key[].
     *
     * Only group items that share the same config-affecting fields:
     *
     *   - deviceID
     *   - kind
     *   - activeValue
     *
     * Runtime fields such as active/startTime are intentionally not written.
     */
    typedef struct itemGroup {
        std::string deviceID;
        Kind kind = Kind::Unknown;
        std::string activeValue = "1";
        std::vector<std::string> keys;
    } itemGroup_t;

    std::map<std::string, itemGroup_t> groups;

    for(const auto& [key, item] : _itemsByKey) {
        std::string groupKey;

        groupKey += item.deviceID;
        groupKey += "|";
        groupKey += std::to_string(static_cast<int>(item.kind));
        groupKey += "|";
        groupKey += item.activeValue;

        if(groups.count(groupKey) == 0) {
            itemGroup_t group = {};

            group.deviceID = item.deviceID;
            group.kind = item.kind;
            group.activeValue = item.activeValue;

            groups[groupKey] = group;
        }

        groups[groupKey].keys.push_back(item.key);
    }

    json items = json::array();

    for(auto& [groupKey, group] : groups) {
        (void)groupKey;

        json entry;

        if(group.keys.size() == 1) {
            entry["key"] = group.keys.front();
        }
        else {
            json keys = json::array();

            for(const auto& key : group.keys) {
                keys.push_back(key);
            }

            entry["key"] = keys;
        }

        switch(group.kind) {
            case Kind::Duration:
                entry["kind"] = "duration";
                break;

            case Kind::Unknown:
            default:
                entry["kind"] = "unknown";
                break;
        }

        if(!group.deviceID.empty()) {
            entry["device"] = group.deviceID;
        }

        /*
         * active_value defaults to true / "1".
         * Only write it if it is not default.
         */
        if(group.activeValue != "1") {
            if(group.activeValue == "0") {
                entry["active_value"] = false;
            }
            else {
                entry["active_value"] = group.activeValue;
            }
        }

        items.push_back(entry);
    }

    config["items"] = items;

    json effects = json::array();

    for(const auto& effect : _actionEffects) {
        json entry;

        entry["device"] = effect.deviceID;
        entry["action"] = effect.action;

        switch(effect.effect) {
            case ActionEffect::Inactive:
                entry["effect"] = "inactive";
                break;

            case ActionEffect::Unknown:
            default:
                entry["effect"] = "unknown";
                break;
        }

        effects.push_back(entry);
    }

    if(!effects.empty()) {
        config["action_effects"] = effects;
    }

    return config;
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

    std::string normalizedDeviceID = normalizeDeviceID(deviceID);

    std::string normalizedAction = action;
    std::transform(normalizedAction.begin(),
                   normalizedAction.end(),
                   normalizedAction.begin(),
                   ::toupper);

    bool handled = false;

    for(const auto& effect : _actionEffects) {
        if(effect.deviceID != normalizedDeviceID) {
            continue;
        }

        if(effect.action != normalizedAction) {
            continue;
        }

        switch(effect.effect) {
            case ActionEffect::Inactive:
                if(forceInactiveForDevice(normalizedDeviceID)) {
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

            if(!entry.contains("key")) {
                LOG_ERROR("TrackingMgr item missing key");
                continue;
            }

            std::vector<std::string> keys;

            if(entry["key"].is_string()) {
                keys.push_back(entry["key"].get<std::string>());
            }
            else if(entry["key"].is_array()) {
                for(const auto& keyEntry : entry["key"]) {
                    if(!keyEntry.is_string()) {
                        LOG_ERROR("TrackingMgr key array contains non-string item");
                        continue;
                    }

                    keys.push_back(keyEntry.get<std::string>());
                }
            }
            else {
                LOG_ERROR("TrackingMgr item key is not string or array");
                continue;
            }

            if(keys.empty()) {
                LOG_ERROR("TrackingMgr item key list is empty");
                continue;
            }

            std::string deviceID;

            if(entry.contains("device") && entry["device"].is_string()) {
                deviceID = normalizeDeviceID(entry["device"].get<std::string>());
            }
            else if(entry.contains("deviceID") && entry["deviceID"].is_string()) {
                deviceID = normalizeDeviceID(entry["deviceID"].get<std::string>());
            }

            Kind kind = Kind::Duration;

            if(entry.contains("kind") && entry["kind"].is_string()) {
                kind = kindForString(entry["kind"].get<std::string>());
            }

            if(kind == Kind::Unknown) {
                LOG_ERROR("TrackingMgr item has unknown kind");
                continue;
            }

            std::string activeValue = "1";

            if(entry.contains("active_value")) {
                activeValue = normalizeValue(entry["active_value"]);
            }

            for(auto key : keys) {
                std::transform(key.begin(),
                               key.end(),
                               key.begin(),
                               ::toupper);

                trackingItem_t item = {};

                item.key = key;
                item.deviceID = deviceID;
                item.kind = kind;
                item.activeValue = activeValue;
                item.active = false;
                item.startTime = 0;

                _itemsByKey[item.key] = item;
            }
        }
    }

    if(config.contains("action_effects") && config["action_effects"].is_array()) {
        for(const auto& entry : config["action_effects"]) {
            if(!entry.is_object()) {
                continue;
            }

            trackingActionEffect_t effect = {};

            if(entry.contains("device") && entry["device"].is_string()) {
                effect.deviceID = normalizeDeviceID(entry["device"].get<std::string>());
            }
            else if(entry.contains("deviceID") && entry["deviceID"].is_string()) {
                effect.deviceID = normalizeDeviceID(entry["deviceID"].get<std::string>());
            }

            if(entry.contains("action") && entry["action"].is_string()) {
                effect.action = entry["action"].get<std::string>();

                std::transform(effect.action.begin(),
                               effect.action.end(),
                               effect.action.begin(),
                               ::toupper);
            }

            if(entry.contains("effect") && entry["effect"].is_string()) {
                effect.effect = actionEffectForString(entry["effect"].get<std::string>());
            }

            if(effect.deviceID.empty()
               || effect.action.empty()
               || effect.effect == ActionEffect::Unknown) {
                LOG_ERROR("TrackingMgr action_effect malformed");
                continue;
            }

            _actionEffects.push_back(effect);
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
    std::string normalizedDeviceID = normalizeDeviceID(deviceID);

    bool handled = false;

    for(auto& [key, item] : _itemsByKey) {
        if(item.deviceID != normalizedDeviceID) {
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

std::string TrackingMgr::normalizeDeviceID(const std::string& str)
{
    std::string s = str;
    std::transform(s.begin(),
                   s.end(),
                   s.begin(),
                   ::toupper);
    return s;
}


std::string TrackingMgr::printString() const
{
  //  std::lock_guard<std::mutex> lock(_mutex);

    std::stringstream ss;

    ss << "TrackingMgr"
       << " setup=" << (_isSetup ? "true" : "false")
       << " items=" << _itemsByKey.size()
       << " action_effects=" << _actionEffects.size()
       << "\n";

    if(_itemsByKey.empty()) {
        ss << "  items: none\n";
    }
    else {
        ss << "  items:\n";

        for(const auto& [key, item] : _itemsByKey) {
            ss << "    "
               << "key=" << item.key
               << " kind=" << stringForKind(item.kind)
               << " active_value=" << item.activeValue
               << " active=" << (item.active ? "true" : "false")
               << " start_time=" << static_cast<long>(item.startTime);

            if(!item.deviceID.empty()) {
                ss << " device=" << item.deviceID;
            }

            ss << "\n";
        }
    }

    if(_actionEffects.empty()) {
        ss << "  action_effects: none\n";
    }
    else {
        ss << "  action_effects:\n";

        for(const auto& effect : _actionEffects) {
            ss << "    "
               << "device=" << effect.deviceID
               << " action=" << effect.action
               << " effect=" << stringForActionEffect(effect.effect)
               << "\n";
        }
    }

    return ss.str();
}

void TrackingMgr::dumpTracking() const
{
    printf("%s\n", printString().c_str());
}

std::string TrackingMgr::stringForKind(Kind kind)
{
    switch(kind) {
        case Kind::Duration:
            return "duration";

        case Kind::Unknown:
        default:
            return "unknown";
    }
}

std::string TrackingMgr::stringForActionEffect(ActionEffect effect)
{
    switch(effect) {
        case ActionEffect::Inactive:
            return "inactive";

        case ActionEffect::Unknown:
        default:
            return "unknown";
    }
}
