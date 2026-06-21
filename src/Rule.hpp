//
//  Rule.hpp
//  pIoTServer
//
//  Created by vinnie on 6/20/26.
//

#ifndef Rule_hpp
#define Rule_hpp

#include <string>
#include <vector>
#include <ctime>

#include "EventAction.hpp"
#include "CommonDefs.hpp"

using namespace std;


// -----------------------------------------------------------------------------
// Rule
//
// A Rule watches an expression condition and runs an immediate list of actions
// when the condition becomes active. It may also watch a separate clear
// condition and run a separate list of clear actions when the rule clears.
//
// Unlike Sequence:
//   - Rule has no steps.
//   - Rule has no per-step duration.
//   - Rule actions run immediately in array order, like abort actions.
//   - Rule owns latch/active state.
// -----------------------------------------------------------------------------

typedef unsigned short ruleID_t;

bool str_to_RuleID(const char* str, ruleID_t* idOut = NULL);
string RuleID_to_string(ruleID_t ruleID);


// ruleState_t is runtime state, not really config.
// It is kept simple for now so RuleMgr can decide how much of it gets persisted.


class Rule {

    friend class pIoTServerDB;

public:

    Rule();
    Rule(nlohmann::json j);
    Rule(std::string);

    Rule(const Rule& right) {
        copy(right, this);
    }

    const std::string printString();

    std::string idString() const;
    nlohmann::json JSON();

    bool isValid();

    ruleID_t ruleID() const { return _rawRuleID; }

    bool isEnabled() { return _enable; }
    void setEnable(bool val) { _enable = val; }

    bool shouldIgnoreManualMode() { return _overrideManualMode; }
    void setShouldIgnoreManualMode(bool val) { _overrideManualMode = val; }

    void setName(std::string name) {
        _name = name;
    }

    void setDescription(std::string desc) {
        _description = desc;
    }

    string name() { return _name; }
    string description() { return _description; }

    void setCondition(std::string cond) {
        _condition = cond;
    }

    void setClearCondition(std::string cond) {
        _clearCondition = cond;
    }

    string condition() { return _condition; }
    string clearCondition() { return _clearCondition; }

    bool hasCondition() {
        return !_condition.empty();
    }

    bool hasClearCondition() {
        return !_clearCondition.empty();
    }

    bool hasActions() {
        return !_actions.empty();
    }

    bool hasClearActions() {
        return !_clearActions.empty();
    }

    void getActions(vector<Action>& act) {
        act = _actions;
    }

    void getClearActions(vector<Action>& act) {
        act = _clearActions;
    }

    bool hasCallBackAction();
    bool hasClearCallBackAction();

    void setIsActive(bool active) {
        _isActive = active;
    }

    bool isActive() {
        return _isActive;
    }

    void setTriggerDelay(uint64_t sec) {
        _triggerDelay = sec;
    }

    void setClearDelay(uint64_t sec) {
        _clearDelay = sec;
    }

    uint64_t triggerDelay() { return _triggerDelay; }
    uint64_t clearDelay() { return _clearDelay; }

    time_t conditionTrueSince() { return _conditionTrueSince; }
    time_t clearTrueSince() { return _clearTrueSince; }

    void setConditionTrueSince(time_t t) {
        _conditionTrueSince = t;
    }

    void setClearTrueSince(time_t t) {
        _clearTrueSince = t;
    }

    time_t lastActionTime() { return _lastActionTime; }
    time_t lastClearActionTime() { return _lastClearActionTime; }

    void setLastActionTime(time_t t) {
        _lastActionTime = t;
    }

    void setLastClearActionTime(time_t t) {
        _lastClearActionTime = t;
    }

    bool lastEvalValid() { return _lastEvalValid; }
    void setLastEvalValid(bool valid) { _lastEvalValid = valid; }

    double lastEvalResult() { return _lastEvalResult; }
    void setLastEvalResult(double val) { _lastEvalResult = val; }

    bool lastClearEvalValid() { return _lastClearEvalValid; }
    void setLastClearEvalValid(bool valid) { _lastClearEvalValid = valid; }

    double lastClearEvalResult() { return _lastClearEvalResult; }
    void setLastClearEvalResult(double val) { _lastClearEvalResult = val; }

    bool isEqual(Rule a) {
        return a._rawRuleID == _rawRuleID;
    }

    bool isEqual(ruleID_t ruleID) {
        return ruleID == _rawRuleID;
    }

    inline bool operator==(const Rule& right) const {
        return right._rawRuleID == _rawRuleID;
    }

    inline bool operator!=(const Rule& right) const {
        return right._rawRuleID != _rawRuleID;
    }

    inline Rule& operator=(const Rule& right) {
        if(this != &right) {
            copy(right, this);
        }

        return *this;
    }

    void resetRuntimeState();

private:

    void initWithJSON(nlohmann::json j);
    void commonInit();

    void copy(const Rule& rule, Rule* ruleOut);

protected:

    ruleID_t        _rawRuleID;

    std::string     _name;
    std::string     _description;

    // Expression evaluated by RuleMgr.
    std::string     _condition;

    // Optional expression used to clear the rule.
    // If empty, RuleMgr may treat "not condition" as the clear condition.
    std::string     _clearCondition;

    // Immediate actions run when the rule transitions inactive -> active.
    vector<Action>  _actions;

    // Immediate actions run when the rule transitions active -> inactive.
    vector<Action>  _clearActions;

    bool            _enable;
    bool            _overrideManualMode;    // doesnt require keys to be in manual mode

    // Runtime latch state.
    bool            _isActive;

    // Runtime evaluation state.
    bool            _lastEvalValid;
    double          _lastEvalResult;

    bool            _lastClearEvalValid;
    double          _lastClearEvalResult;

    // Hysteresis / debounce timing.
    // condition must remain true for _triggerDelay before action[] runs.
    // clear_condition must remain true for _clearDelay before clear_action[] runs.
    uint64_t        _triggerDelay;
    uint64_t        _clearDelay;

    // Runtime timestamps, in localNow units.
    time_t          _conditionTrueSince;
    time_t          _clearTrueSince;
    time_t          _lastActionTime;
    time_t          _lastClearActionTime;
};


#endif /* Rule_hpp */
