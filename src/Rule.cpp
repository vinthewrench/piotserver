//
//  Rule.cpp
//  pIoTServer
//
//  Created by vinnie on 6/20/26.
//

#include "Rule.hpp"

#include "PropValKeys.hpp"
#include "CommonDefs.hpp"
#include "Utils.hpp"

using namespace nlohmann;
using namespace std;


// -----------------------------------------------------------------------------
// Private helpers
// -----------------------------------------------------------------------------

static void parseActionList(json j, vector<Action>& actionsOut) {
    vector<Action> actions;

    if(j.is_object()) {
        Action a = Action(j);
        if(a.isValid()) {
            actions.push_back(a);
        }
    }
    else if(j.is_array()) {
        for(auto jA : j) {
            Action a = Action(jA);
            if(a.isValid()) {
                actions.push_back(a);
            }
        }
    }

    actionsOut = actions;
}


static json actionListJSON(vector<Action>& actions) {
    json jActions;

    for(auto e : actions) {
        jActions.push_back(e.JSON());
    }

    return jActions;
}


// -----------------------------------------------------------------------------
// Rule ID helpers
// -----------------------------------------------------------------------------

bool str_to_RuleID(const char* str, ruleID_t* idOut) {
    bool status = false;

    ruleID_t val = 0;

    status = sscanf(str, "%hx", &val) == 1;

    if(idOut) {
        *idOut = val;
    }

    return status;
}


string RuleID_to_string(ruleID_t ruleID) {
    return to_hex<unsigned short>(ruleID);
}


// -----------------------------------------------------------------------------
// Rule
// -----------------------------------------------------------------------------

void Rule::commonInit() {
    _rawRuleID = 0;

    _name.clear();
    _description.clear();

    _condition.clear();
    _clearCondition.clear();

    _actions.clear();
    _clearActions.clear();

    _enable = true;
    _overrideManualMode = false;

    _isActive = false;

    _lastEvalValid = false;
    _lastEvalResult = 0;

    _lastClearEvalValid = false;
    _lastClearEvalResult = 0;

    _triggerDelay = 0;
    _clearDelay = 0;

    _conditionTrueSince = 0;
    _clearTrueSince = 0;
    _lastActionTime = 0;
    _lastClearActionTime = 0;
}


Rule::Rule() {
    commonInit();
}


Rule::Rule(std::string str) {
    commonInit();

    json j = json::parse(str);
    initWithJSON(j);
}


Rule::Rule(nlohmann::json j) {
    initWithJSON(j);
}


void Rule::initWithJSON(nlohmann::json j) {
    commonInit();

    if(j.contains(PROP_ARG_RULE_ID)
       && j.at(string(PROP_ARG_RULE_ID)).is_string()) {
        string jID = j.at(string(PROP_ARG_RULE_ID));
        str_to_RuleID(jID.c_str(), &_rawRuleID);
    }

    if(j.contains(JSON_ARG_ENABLE)
       && j.at(string(JSON_ARG_ENABLE)).is_boolean()) {
        _enable = j.at(string(JSON_ARG_ENABLE));
    }

    if(j.contains(JSON_ARG_OVERIDE_MANUAL)
       && j.at(string(JSON_ARG_OVERIDE_MANUAL)).is_boolean()) {
        _overrideManualMode = j.at(string(JSON_ARG_OVERIDE_MANUAL));
    }

    if(j.contains(JSON_ARG_NAME)
       && j.at(string(JSON_ARG_NAME)).is_string()) {
        _name = j.at(string(JSON_ARG_NAME));
    }

    if(j.contains(PROP_DESCRIPTION)
       && j.at(string(PROP_DESCRIPTION)).is_string()) {
        _description = j.at(string(PROP_DESCRIPTION));
    }

    if(j.contains(JSON_ARG_CONDITION)
       && j.at(string(JSON_ARG_CONDITION)).is_string()) {
        _condition = j.at(string(JSON_ARG_CONDITION));
    }

    if(j.contains(JSON_ARG_CLEAR_CONDITION)
       && j.at(string(JSON_ARG_CLEAR_CONDITION)).is_string()) {
        _clearCondition = j.at(string(JSON_ARG_CLEAR_CONDITION));
    }

    if(j.contains(JSON_ARG_ACTION)) {
        parseActionList(j.at(string(JSON_ARG_ACTION)), _actions);
    }

    if(j.contains(JSON_ARG_CLEAR_ACTION)) {
        parseActionList(j.at(string(JSON_ARG_CLEAR_ACTION)), _clearActions);
    }

    if(j.contains(JSON_ARG_TRIGGER_DELAY)
       && j.at(string(JSON_ARG_TRIGGER_DELAY)).is_number_unsigned()) {
        _triggerDelay = j.at(string(JSON_ARG_TRIGGER_DELAY));
    }

    if(j.contains(JSON_ARG_CLEAR_DELAY)
       && j.at(string(JSON_ARG_CLEAR_DELAY)).is_number_unsigned()) {
        _clearDelay = j.at(string(JSON_ARG_CLEAR_DELAY));
    }
}


nlohmann::json Rule::JSON() {
    json j;

    j[string(PROP_ARG_RULE_ID)] = RuleID_to_string(_rawRuleID);

    if(!_name.empty()) {
        j[string(JSON_ARG_NAME)] = _name;
    }

    if(!_description.empty()) {
        j[string(PROP_DESCRIPTION)] = _description;
    }

    if(!_condition.empty()) {
        j[string(JSON_ARG_CONDITION)] = _condition;
    }

    if(!_clearCondition.empty()) {
        j[string(JSON_ARG_CLEAR_CONDITION)] = _clearCondition;
    }

    if(_actions.size()) {
        j[string(JSON_ARG_ACTION)] = actionListJSON(_actions);
    }

    if(_clearActions.size()) {
        j[string(JSON_ARG_CLEAR_ACTION)] = actionListJSON(_clearActions);
    }

    if(_triggerDelay) {
        j[string(JSON_ARG_TRIGGER_DELAY)] = _triggerDelay;
    }

    if(_clearDelay) {
        j[string(JSON_ARG_CLEAR_DELAY)] = _clearDelay;
    }

    j[string(JSON_ARG_ENABLE)] = _enable;
    j[string(JSON_ARG_OVERIDE_MANUAL)] = _overrideManualMode;

    return j;
}


bool Rule::isValid() {
    if(_rawRuleID == 0) {
        return false;
    }

    if(_condition.empty()) {
        return false;
    }

    for(auto a : _actions) {
        if(!a.isValid()) {
            return false;
        }
    }

    for(auto a : _clearActions) {
        if(!a.isValid()) {
            return false;
        }
    }

    return true;
}


void Rule::copy(const Rule& rule, Rule* ruleOut) {
    ruleOut->_rawRuleID          = rule._rawRuleID;

    ruleOut->_name               = rule._name;
    ruleOut->_description        = rule._description;

    ruleOut->_condition          = rule._condition;
    ruleOut->_clearCondition     = rule._clearCondition;

    ruleOut->_actions            = rule._actions;
    ruleOut->_clearActions       = rule._clearActions;

    ruleOut->_enable             = rule._enable;
    ruleOut->_overrideManualMode = rule._overrideManualMode;

    ruleOut->_isActive           = rule._isActive;

    ruleOut->_lastEvalValid      = rule._lastEvalValid;
    ruleOut->_lastEvalResult     = rule._lastEvalResult;

    ruleOut->_lastClearEvalValid  = rule._lastClearEvalValid;
    ruleOut->_lastClearEvalResult = rule._lastClearEvalResult;

    ruleOut->_triggerDelay       = rule._triggerDelay;
    ruleOut->_clearDelay         = rule._clearDelay;

    ruleOut->_conditionTrueSince = rule._conditionTrueSince;
    ruleOut->_clearTrueSince     = rule._clearTrueSince;
    ruleOut->_lastActionTime     = rule._lastActionTime;
    ruleOut->_lastClearActionTime = rule._lastClearActionTime;
}


std::string Rule::idString() const {
    return RuleID_to_string(_rawRuleID);
}


void Rule::resetRuntimeState() {
    _isActive = false;

    _lastEvalValid = false;
    _lastEvalResult = 0;

    _lastClearEvalValid = false;
    _lastClearEvalResult = 0;

    _conditionTrueSince = 0;
    _clearTrueSince = 0;
    _lastActionTime = 0;
    _lastClearActionTime = 0;
}


bool Rule::hasCallBackAction() {
    for(auto e : _actions) {
        if(e.isCallBack()) {
            return true;
        }
    }

    return false;
}


bool Rule::hasClearCallBackAction() {
    for(auto e : _clearActions) {
        if(e.isCallBack()) {
            return true;
        }
    }

    return false;
}


const std::string Rule::printString() {
    std::ostringstream oss;

    oss << "<" << RuleID_to_string(_rawRuleID) << ">";

    if(_name.size()) {
        oss << " \"" << _name << "\"";
    }

    if(!_enable) {
        oss << " disabled";
    }

    oss << (_isActive ? " active" : " not active") << endl;

    if(_description.size()) {
        oss << "description: " << _description << endl;
    }

    if(_condition.size()) {
        oss << "condition: " << _condition << endl;
    }

    if(_clearCondition.size()) {
        oss << "clear condition: " << _clearCondition << endl;
    }

    if(_triggerDelay) {
        oss << "trigger_delay: " << _triggerDelay << endl;
    }

    if(_clearDelay) {
        oss << "clear_delay: " << _clearDelay << endl;
    }

    if(_actions.size()) {
        oss << "\taction:" << endl;
        for(auto a : _actions) {
            oss << "\t  " << a.printString() << endl;
        }
    }

    if(_clearActions.size()) {
        oss << "\tclear_action:" << endl;
        for(auto a : _clearActions) {
            oss << "\t  " << a.printString() << endl;
        }
    }

    oss << endl;

    return oss.str();
}
