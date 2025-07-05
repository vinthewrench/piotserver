//
//  Sequence.hpp
//  pIoTServer
//
//  Created by vinnie on 2/13/25.
//

#ifndef Sequence_hpp
#define Sequence_hpp
#include <set>
#include "EventAction.hpp"
#include "CommonDefs.hpp"


// step is an a list of action with a duration
class Step {
    
    friend class Sequence;

public:
    
    Step();
    Step(nlohmann::json j);
    Step(std::string);
     
    const std::string printString();
    
    nlohmann::json JSON();
    
    bool isValid();
    
    void getActions(vector<Action> & act) {
        act = _actions;
    }
  
    void setName(std::string name){
        _name = name;
    }

     void setDescription(std::string desc){
        _description = desc;
    }
    
    string name() { return _name;};
    string description() { return _description;};

    uint64_t duration() { return _duration; };

    bool hasCallBackAction();
 
private:
    void initWithJSON(nlohmann::json j);

protected:
  
    std::string     _name;
    std::string     _description;
 
    vector<Action>      _actions;
    uint64_t            _duration;
};


class AbortAction {
    
    friend class Sequence;

public:
    
    AbortAction();
    AbortAction(nlohmann::json j);
    AbortAction(std::string);
     
    const std::string printString();
    
    nlohmann::json JSON();
    
    bool isValid();
    
    void getActions(vector<Action> & act) {
        act = _actions;
    }

    bool hasCallBackAction();
 
private:
    void initWithJSON(nlohmann::json j);

protected:
  
    vector<Action>      _actions;
 };

typedef  unsigned short sequenceID_t;

bool str_to_SequenceID(const char* str, sequenceID_t *idOut = NULL);
string  SequenceID_to_string(sequenceID_t seqID);

typedef struct {
     string          value;
     uint            stepNo;
    uint64_t         offset;
} scheduleStepEntry_t;


class Sequence {
    friend class pIoTServerDB;

public:
    Sequence();
    Sequence(nlohmann::json j);
    Sequence(std::string);
    Sequence(EventTrigger trigger, Action action);
   
    const std::string printString();
  
    std::string idString() const;
     nlohmann::json JSON();
    
    bool isValid();
    const sequenceID_t  sequenceID(){return _rawSequenceID;};
    bool isEnabled(){ return _enable;};
    void setEnable(bool val) { _enable = val;};

    bool shouldIgnoreLog(){ return _dontLog;};
    void setSouldIgnoreLog(bool val) { _dontLog = val;};

    bool wasManuallyTriggered(){ return _wasManuallyTriggered;};
    void setManuallyTriggered(bool val) { _wasManuallyTriggered = val;};

    void setName(std::string name){
        _name = name;
    }

     void setDescription(std::string desc){
        _description = desc;
    }

    void setCondition(std::string cond){
        _condition = cond;
   }

    string name() { return _name;};
    string description() { return _description;};
    string condition() { return _condition;};

    void getSteps(vector<Step> & stp) {
        stp = _steps;
    }
  
    bool getStep(uint stepNo, Step &stp);
    
    uint stepsCount() { return  (uint) _steps.size();};
    
    uint64_t stepsDuration();
    
    bool hasCallBackAction();
  
    bool hasAbortAction();
    
    bool getAbortActions(vector<Action> & act);
 
    bool hasCondition() { return  !_condition.empty();  };
 
    void setEventTrigger(EventTrigger trig){
        _trigger = trig;
    }

    bool isAppEvent() {
        return _trigger.isAppEvent();
    }
    
    reference_wrapper<EventTrigger> getEventTrigger() {
        return  std::ref(_trigger);
    }
  
    bool isEqual(Sequence a) {
        return a._rawSequenceID  == _rawSequenceID ;
    }

    bool isEqual(sequenceID_t sequenceID) {
        return sequenceID  == _rawSequenceID ;
    }

    inline bool operator==(const Sequence& right) const {
        return right._rawSequenceID  == _rawSequenceID;
        }

    inline bool operator!=(const Sequence& right) const {
        return right._rawSequenceID  != _rawSequenceID;
    }

    inline void operator = (const Sequence &right ) {
        copy(right, this);
    }
    
    void resetSteps();
    
    bool shouldRunSequenceFromAppEvent(EventTrigger::app_event_t a, int &stepNo);
 
    bool scheduleForValue(string valueKey, vector<scheduleStepEntry_t> &ssEntry);
 
    bool allKeysInSequence(std::set<std::string> &keys);
 
     
private:
    void initWithJSON(nlohmann::json j);
    void commonInit();
    
    void copy(const Sequence &seq, Sequence *seqOut);
 
protected:
    
    sequenceID_t    _rawSequenceID;
    std::string     _name;
    std::string     _description;
    EventTrigger    _trigger;
    vector<Step>    _steps;
    std::string     _condition;
    AbortAction     _onAbort;
    
    bool            _enable;
    bool            _wasManuallyTriggered;
    bool            _overrideManualMode;    // doesnt require keys to be in manual mode
    
    uint            _nextStepToRun;
    time_t          _lastStepRunTime;  // in localNow units
    
    bool            _dontLog;
  };



#endif /* Sequence_hpp */
