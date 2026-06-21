//
//  pIoTServerDB_Rules.cpp
//  pIoTServer
//
//  Split from pIoTServerDB.cpp
//

#include "pIoTServerDB.hpp"
#include "PropValKeys.hpp"

#include <algorithm>
#include <array>
#include <bitset>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <strings.h>
#include <tuple>
#include <vector>

#include <time.h>

#include "json.hpp"
#include "Utils.hpp"
#include "LogMgr.hpp"
#include "SolarTimeMgr.hpp"
#include "TimeStamp.hpp"
#include "lunar.hpp"
#include "Actuator_Device.hpp"
#include "pIoTServerEvaluator.hpp"

using namespace timestamp;
using namespace nlohmann;
using namespace std;

#define DBL_MAX  std::numeric_limits<double>::max()
#define TIME_MAX std::numeric_limits<time_t>::max()


// MARK: - Rules
//
// Rule storage notes:
//
//   - Rule config changes call saveProperties().
//   - Most runtime timing fields do NOT call saveProperties().
//     Those fields can change on every rule evaluation pass, and persisting
//     them constantly would be noisy and rough on flash/SD storage.
//
// Runtime fields include:
//
//   - _conditionTrueSince
//   - _clearTrueSince
//   - _lastActionTime
//   - _lastClearActionTime
//
// _isActive is runtime latch state. For now ruleSetActive() persists because
// active state is important across manager/API calls. We can revisit later if
// we decide active state should be purely volatile.
//

bool pIoTServerDB::ruleIDIsValid(ruleID_t rid) {

    return (_rules.count(rid) > 0);
}


bool pIoTServerDB::ruleFind(string name, ruleID_t &ridsOut) {

    for(auto e : _rules) {
        auto rule = &e.second;

        if(strcasecmp(name.c_str(), rule->_name.c_str()) == 0) {
            ridsOut = e.first;
            return true;
        }
    }

    return false;
}


vector<ruleID_t> pIoTServerDB::allruleIDs() {

    vector<ruleID_t> rids;

    for(const auto& [key, _] : _rules) {
        rids.push_back(key);
    }

    return rids;
}


json pIoTServerDB::ruleJSON(ruleID_t rid) {

    json j;

    if(_rules.count(rid)) {
        j = _rules[rid].JSON();
    }

    return j;
}


// createUniqueRuleID() is called by ruleSave() while _mutex is already held.
ruleID_t pIoTServerDB::createUniqueRuleID() {
    std::uniform_int_distribution<long> distribution(SHRT_MIN, SHRT_MAX);

    ruleID_t rid;

    do {
        rid = distribution(_rng);
    } while(_rules.count(rid) > 0);

    return rid;
}


bool pIoTServerDB::ruleDelete(ruleID_t rid) {
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(_rules.count(rid) == 0) {
            return false;
        }

        _rules.erase(rid);
    }

    saveProperties();
    return true;
}


bool pIoTServerDB::ruleSave(Rule rule, ruleID_t* ridOut) {
    ruleID_t rid;

    {
        std::lock_guard<std::mutex> lock(_mutex);

        rid = createUniqueRuleID();

        rule._rawRuleID = rid;
        _rules[rid] = rule;
    }

    saveProperties();

    if(ridOut) {
        *ridOut = rid;
    }

    return true;
}


bool pIoTServerDB::ruleUpdate(ruleID_t rid, Rule rule) {
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(_rules.count(rid) == 0) {
            return false;
        }

        /*
         * The caller is updating the rule at rid.
         * Keep the stored rule ID authoritative even if the passed Rule object
         * has a stale/default ID.
         */
        rule._rawRuleID = rid;
        _rules[rid] = rule;
    }

    saveProperties();
    return true;
}


string pIoTServerDB::ruleGetName(ruleID_t rid) {

    if(_rules.count(rid) == 0) {
        return "";
    }

    return _rules[rid]._name;
}


bool pIoTServerDB::ruleSetName(ruleID_t rid, string name) {
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(_rules.count(rid) == 0) {
            return false;
        }

        Rule* rule = &_rules[rid];
        rule->_name = name;
    }

    saveProperties();
    return true;
}


bool pIoTServerDB::ruleSetDescription(ruleID_t rid, string desc) {
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(_rules.count(rid) == 0) {
            return false;
        }

        Rule* rule = &_rules[rid];
        rule->_description = desc;
    }

    saveProperties();
    return true;
}


bool pIoTServerDB::ruleSetEnable(ruleID_t rid, bool enable) {
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(_rules.count(rid) == 0) {
            return false;
        }

        Rule* rule = &_rules[rid];
        rule->_enable = enable;
    }

    saveProperties();
    return true;
}


bool pIoTServerDB::ruleIsEnabled(ruleID_t rid) {

    if(_rules.count(rid) == 0) {
        return false;
    }

    return _rules[rid]._enable;
}


bool pIoTServerDB::ruleSetActive(ruleID_t rid, bool active) {
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(_rules.count(rid) == 0) {
            return false;
        }

        Rule* rule = &_rules[rid];
        rule->_isActive = active;
    }

    saveProperties();
    return true;
}


bool pIoTServerDB::ruleIsActive(ruleID_t rid) {

    if(_rules.count(rid) == 0) {
        return false;
    }

    return _rules[rid]._isActive;
}


string pIoTServerDB::ruleGetCondition(ruleID_t rid) {

    if(_rules.count(rid) == 0) {
        return "";
    }

    return _rules[rid]._condition;
}


string pIoTServerDB::ruleGetClearCondition(ruleID_t rid) {
    std::lock_guard<std::mutex> lock(_mutex);

    if(_rules.count(rid) == 0) {
        return "";
    }

    return _rules[rid]._clearCondition;
}


uint64_t pIoTServerDB::ruleGetTriggerDelay(ruleID_t rid) {

    if(_rules.count(rid) == 0) {
        return 0;
    }

    return _rules[rid]._triggerDelay;
}


uint64_t pIoTServerDB::ruleGetClearDelay(ruleID_t rid) {
    std::lock_guard<std::mutex> lock(_mutex);

    if(_rules.count(rid) == 0) {
        return 0;
    }

    return _rules[rid]._clearDelay;
}


time_t pIoTServerDB::ruleGetConditionTrueSince(ruleID_t rid) {

    if(_rules.count(rid) == 0) {
        return 0;
    }

    return _rules[rid]._conditionTrueSince;
}


bool pIoTServerDB::ruleSetConditionTrueSince(ruleID_t rid, time_t t) {
    std::lock_guard<std::mutex> lock(_mutex);

    if(_rules.count(rid) == 0) {
        return false;
    }

    _rules[rid]._conditionTrueSince = t;
    return true;
}


time_t pIoTServerDB::ruleGetClearTrueSince(ruleID_t rid) {

    if(_rules.count(rid) == 0) {
        return 0;
    }

    return _rules[rid]._clearTrueSince;
}


bool pIoTServerDB::ruleSetClearTrueSince(ruleID_t rid, time_t t) {
    std::lock_guard<std::mutex> lock(_mutex);

    if(_rules.count(rid) == 0) {
        return false;
    }

    _rules[rid]._clearTrueSince = t;
    return true;
}


bool pIoTServerDB::ruleSetLastActionTime(ruleID_t rid, time_t t) {
    std::lock_guard<std::mutex> lock(_mutex);

    if(_rules.count(rid) == 0) {
        return false;
    }

    _rules[rid]._lastActionTime = t;
    return true;
}


bool pIoTServerDB::ruleSetLastClearActionTime(ruleID_t rid, time_t t) {
    std::lock_guard<std::mutex> lock(_mutex);

    if(_rules.count(rid) == 0) {
        return false;
    }

    _rules[rid]._lastClearActionTime = t;
    return true;
}


bool pIoTServerDB::ruleGetActions(ruleID_t rid, vector<Action>& actions) {

    actions.clear();

    if(_rules.count(rid) == 0) {
        return false;
    }

    actions = _rules[rid]._actions;
    return true;
}


bool pIoTServerDB::ruleGetClearActions(ruleID_t rid, vector<Action>& actions) {

    actions.clear();

    if(_rules.count(rid) == 0) {
        return false;
    }

    actions = _rules[rid]._clearActions;
    return true;
}


// Manual trigger support.
//
// This function only forces the rule latch active in the DB.
// It does NOT evaluate the rule condition.
// It does NOT run action[].
// It does NOT run clear_action[].
//
// Action execution belongs in pIoTServerMgr. If the REST/API path wants a
// manual trigger to actually perform action[], the manager should run those
// actions and then call this or otherwise set active state.
bool pIoTServerDB::triggerRule(ruleID_t rid) {
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(_rules.count(rid) == 0) {
            return false;
        }

        Rule* rule = &_rules[rid];

        if(!rule->_enable) {
            return false;
        }

        /*
         * Manual trigger means force the rule active.
         * If it is already active, this is a no-op success.
         */
        if(rule->_isActive) {
            return true;
        }

        time_t now = time(NULL);
        struct tm* tm = localtime(&now);
        time_t localNow = now;

        if(tm) {
            localNow = now + tm->tm_gmtoff;
        }

        rule->_isActive = true;
        rule->_lastActionTime = localNow;
        rule->_conditionTrueSince = 0;
        rule->_clearTrueSince = 0;
    }

    saveProperties();
    return true;
}
