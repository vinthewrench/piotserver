//
//  Sequence.cpp
//  pIoTServer
//
//  Created by vinnie on 2/13/25.
//

#include "Sequence.hpp"

#include "PropValKeys.hpp"
#include "CommonDefs.hpp"  // for stringvector

#include "Utils.hpp"

using namespace nlohmann;
using namespace std;

// MARK: -   STEPS

Step::Step(){
    _duration = 0;
    _actions .clear();
}

Step::Step(nlohmann::json j) {
    initWithJSON(j);
}
 
 
void Step::initWithJSON(nlohmann::json j){
    _duration = 0;
    _actions .clear();
    
    if( j.contains(JSON_ARG_DURATION) && j.at(JSON_ARG_DURATION).is_number_unsigned()){
        _duration = j.at(JSON_ARG_DURATION);
    }
    else _duration = 0;
    
    if(j.contains(JSON_ARG_NAME)
        && j.at(string(JSON_ARG_NAME)).is_string()) {
        auto jS = j.at(string(JSON_ARG_NAME));
        _name = jS;
    }
    
    if(j.contains(PROP_DESCRIPTION)
          && j.at(string(PROP_DESCRIPTION)).is_string()) {
          auto jS = j.at(string(PROP_DESCRIPTION));
        _description = jS;
      }

   if( j.contains(JSON_ARG_ACTION)){
        json jAct = j.at(JSON_ARG_ACTION);
        
        vector<Action> actions;
        actions.clear();
        
        if(jAct.is_object()){
            Action a = Action(jAct);
            if(a.isValid()){
                actions.push_back(a);
            }
        }
        else if(jAct.is_array()){
            for(auto jA : jAct){
                Action a = Action(jA);
                if(a.isValid()){
                    actions.push_back(a);
                }
            }
        }
        _actions = actions;
    }
}


Step::Step(std::string str){
    _duration = 0;
    _actions .clear();

    json j;
    j  = json::parse(str);
    initWithJSON(j);
}

bool Step::isValid(){
    
    if(_actions.size()){
        for(auto a :_actions) {
            if(!a.isValid()) return false;
        }
    }
    
    return true;;
}

 
nlohmann::json Step::JSON(){
    json j;
   
    if(_duration)  j[JSON_ARG_DURATION] = _duration;
    
    if(!_name.empty()) j[string(JSON_ARG_NAME)] = _name;
    if(!_description.empty()) j[string(PROP_DESCRIPTION)] = _description;

    json jActions;
    for(auto e: _actions){
        // dont export callbacks.
        auto jA = e.JSON();
        jActions.push_back(jA);
    }
    
    j[JSON_ARG_ACTION] = jActions;

    return j;
}



bool Step::hasCallBackAction(){
    
    for(auto e: _actions){
        if(e.isCallBack()) return true;
    }
    return false;
}


const std::string Step:: printString(){
    std::ostringstream oss;
    
     if(_name.size())
            oss << "\"" << _name << "\" ";
    
    if(_duration)
        oss <<  "(" << _duration << "s) ";
 
    oss << endl;
    if(_actions.size()){
          for(auto a : _actions)
            oss << "\t  " << a.printString() << endl;
      }
  
    return  oss.str();
};
 
// MARK - AbortAction
 
AbortAction::AbortAction(){
    _actions .clear();
}

AbortAction::AbortAction(nlohmann::json j) {
    initWithJSON(j);
}
 
 
void AbortAction::initWithJSON(nlohmann::json j){
    _actions .clear();
 
   if( j.contains(JSON_ARG_ACTION)){
        json jAct = j.at(JSON_ARG_ACTION);
        
        vector<Action> actions;
        actions.clear();
        
        if(jAct.is_object()){
            Action a = Action(jAct);
            if(a.isValid()){
                actions.push_back(a);
            }
        }
        else if(jAct.is_array()){
            for(auto jA : jAct){
                Action a = Action(jA);
                if(a.isValid()){
                    actions.push_back(a);
                }
            }
        }
        _actions = actions;
    }
}


AbortAction::AbortAction(std::string str){
    _actions .clear();

    json j;
    j  = json::parse(str);
    initWithJSON(j);
}

bool AbortAction::isValid(){
    
    if(_actions.size()){
        for(auto a :_actions) {
            if(!a.isValid()) return false;
        }
        return true;;
    }
    
    return false;
}
 
nlohmann::json AbortAction::JSON(){
    json j;
   
    json jActions;
    for(auto e: _actions){
        // dont export callbacks.
        auto jA = e.JSON();
        jActions.push_back(jA);
    }
    
    j[JSON_ARG_ACTION] = jActions;

    return j;
}




bool AbortAction::hasCallBackAction(){
    
    for(auto e: _actions){
        if(e.isCallBack()) return true;
    }
    return false;
}


const std::string AbortAction:: printString(){
    std::ostringstream oss;

    oss << endl;
    if(_actions.size()){
          for(auto a : _actions)
            oss << "\t  " << a.printString() << endl;
      }
  
    return  oss.str();
};
 

// MARK: -   Sequence

bool str_to_SequenceID(const char* str, sequenceID_t *idOut){
    bool status = false;
    
    sequenceID_t val = 0;
 
    status = sscanf(str, "%hx", &val) == 1;
    
    if(idOut)  {
        *idOut = val;
    }
    
    return status;

}

string  SequenceID_to_string(sequenceID_t seqID){
     return to_hex<unsigned short>(seqID);
}

void Sequence::commonInit(){
    _rawSequenceID = 0;
    _trigger     = EventTrigger();
    _steps.clear();
     _nextStepToRun = 0;
    _lastStepRunTime = 0;
    _dontLog = true;
    _overrideManualMode = false;
}

Sequence::Sequence(){
    commonInit();
 }


Sequence::Sequence(EventTrigger trigger, Action action){
    commonInit();
    _trigger = trigger;
    Step step = Step();
    step._actions.push_back(action);
    _steps.push_back(step);
}


Sequence::Sequence(std::string str){
    commonInit();
    
    json j;
    j  = json::parse(str);
    initWithJSON(j);
}

void Sequence::initWithJSON(nlohmann::json j){
    commonInit();
 
    _rawSequenceID = 0;
   
    if(j.contains(JSON_ARG_SEQUENCE_ID)
        && j.at(string(JSON_ARG_SEQUENCE_ID)).is_string()) {
        string jID = j.at(string(JSON_ARG_SEQUENCE_ID));
        str_to_SequenceID(jID.c_str(), &_rawSequenceID);
    }
  
    if( j.contains(JSON_ARG_ENABLE) && j.at(JSON_ARG_ENABLE).is_boolean()){
        _enable = j.at(JSON_ARG_ENABLE);
    }
    else _enable = true;

    if( j.contains(JSON_ARG_OVERIDE_MANUAL) && j.at(JSON_ARG_OVERIDE_MANUAL).is_boolean()){
        _overrideManualMode = j.at(JSON_ARG_OVERIDE_MANUAL);
    }
    else _overrideManualMode = false;
    
    if( j.contains(JSON_ARG_TRIGGER)
        && j.at(string(JSON_ARG_TRIGGER)).is_object()) {
        auto jT = j.at(string(JSON_ARG_TRIGGER));
        _trigger = EventTrigger(jT);
    }
    else
        _trigger = EventTrigger(EventTrigger::APP_EVENT_MANUAL);
    
    if( j.contains(JSON_ARG_STEPS)){
        json jStep = j.at(JSON_ARG_STEPS);
     
        vector<Step> steps;
        steps.clear();
        
        if(jStep.is_object()){
            Step a = Step(jStep);
            if(a.isValid()){
                steps.push_back(a);
            }
        }
        else if(jStep.is_array()){
            for(auto jA : jStep){
                Step a = Step(jA);
                if(a.isValid()){
                    steps.push_back(a);
                }
            }
        }
        _steps = steps;
     }
    
    if(j.contains(JSON_ARG_CONDITION)
          && j.at(string(JSON_ARG_CONDITION)).is_string()) {
          auto jS = j.at(string(JSON_ARG_CONDITION));
        _condition = jS;
      }

    if(j.contains(JSON_ARG_NAME)
        && j.at(string(JSON_ARG_NAME)).is_string()) {
        auto jS = j.at(string(JSON_ARG_NAME));
        _name = jS;
    }
    
    if(j.contains(PROP_DESCRIPTION)
          && j.at(string(PROP_DESCRIPTION)).is_string()) {
          auto jS = j.at(string(PROP_DESCRIPTION));
        _description = jS;
      }
    
    if( j.contains(JSON_ARG_ON_ABORT)
        && j.at(string(JSON_ARG_ON_ABORT)).is_object()) {
        auto jT = j.at(string(JSON_ARG_ON_ABORT));
        _onAbort = AbortAction(jT);
    }
    else
        _onAbort = AbortAction();

}

Sequence::Sequence(nlohmann::json j){
    initWithJSON(j);
}

 
nlohmann::json Sequence::JSON(){
    json j;
    
    json jSteps;
    for(auto e: _steps){
        // dont export callbacks.
        auto jA = e.JSON();
        jSteps.push_back(jA);
    }
    
    j[JSON_ARG_STEPS] = jSteps;

     auto jT = _trigger.JSON();
    j[string(JSON_ARG_TRIGGER)] = jT;
    
    j[string(JSON_ARG_SEQUENCE_ID)] = to_hex<unsigned short>(_rawSequenceID);
    
    if(!_name.empty()) j[string(JSON_ARG_NAME)] = _name;
    if(!_description.empty()) j[string(PROP_DESCRIPTION)] = _description;
    if(!_condition.empty()) j[string(JSON_ARG_CONDITION)] = _condition;
  
    if(_onAbort.isValid()){
        auto jAB = _onAbort.JSON();
        j[string(JSON_ARG_ON_ABORT)] = jAB;
   }
  
    j[JSON_ARG_ENABLE] = _enable;
    j[JSON_ARG_OVERIDE_MANUAL] = _overrideManualMode;
  
    return j;
}

bool Sequence::isValid(){
  
    if(_steps.size()){
        for(auto a :_steps) {
            if(!a.isValid()) return false;
        }
    }
    return  _trigger.isValid();
}


 
void Sequence::copy(const Sequence &seq1, Sequence *seq2){
    seq2->_rawSequenceID    = seq1._rawSequenceID;
    seq2->_name             = seq1._name;
    seq2->_description      = seq1._description;
    seq2->_trigger          = seq1._trigger;
    seq2->_condition        = seq1._condition;
    seq2->_onAbort          = seq1._onAbort;
    seq2->_steps            = seq1._steps;
    seq2->_enable           = seq1._enable;
    seq2->_nextStepToRun    = seq1._nextStepToRun;
    seq2->_lastStepRunTime  = seq1._lastStepRunTime;
    seq2->_dontLog           = seq1._dontLog;
    seq2->_overrideManualMode = seq1._overrideManualMode;
  }


bool Sequence::getStep(uint stepNo, Step &stp){
    if(stepNo < _steps.size()){
        stp = _steps[stepNo];
        return true;
    }
    
    return false;
}

void Sequence::resetSteps(){
    _nextStepToRun = 0;
    _lastStepRunTime = 0;
 }
 
bool Sequence::shouldRunSequenceFromAppEvent(EventTrigger::app_event_t a, int &stepNo){
    
    if( _trigger.shouldTriggerFromAppEvent(a)){
        if(_nextStepToRun < _steps.size()) {
            stepNo = _nextStepToRun;
            return true;
        }
    }
    return false;
}


uint64_t Sequence::stepsDuration(){
    
    uint64_t total = 0;
    
    if(_steps.size()){
        for(auto a :_steps) {
            total +=  a.duration();
        }
    }
 
    return total;
}
 

bool Sequence::hasCallBackAction(){
    
    for(auto e: _steps){
        if(e.hasCallBackAction()) return true;
    }
    return false;
}

bool Sequence::hasAbortAction(){
    
    return  _onAbort.isValid();
}

bool  Sequence::getAbortActions(vector<Action> & act) {
    if(hasAbortAction()) {
        _onAbort.getActions(act);
        return true;
    }
       return false;
  }


const std::string Sequence::printString(){
    std::ostringstream oss;
    
    oss << "<" << SequenceID_to_string(_rawSequenceID) << ">";
    
    if(_name.size()) oss << " \"" << _name << "\"";
    uint64_t duration = stepsDuration();
     if(duration)
         oss <<  " (" << duration << "s) ";
    oss << " " << _trigger.printString() << endl;
   
    if(_condition.size()) oss << "condition: " << _condition << endl;

    auto count = _steps.size();
    if(count){
        int i = 0;
        for(auto a : _steps){
            oss <<  "\tstep:" <<  i++  << " " << a.printString();
        }
    }
    
    oss << endl;

    return  oss.str();
};


/*
 Walk the sequence looking for actions that effect the specified variable;
 
 typedef struct {
      string          value;
      uint            stepNo;
 } ScheduleStepEntry_t;

 
 */

bool Sequence::scheduleForValue(string valueKey, vector<scheduleStepEntry_t> &ssEntry){
    bool found = false;
    
    vector<scheduleStepEntry_t> entries;
    uint64_t   offset = 0;
    
    for(uint stepNo = 0; stepNo < _steps.size(); stepNo++){
        auto &stp =  _steps[stepNo];
        for(auto &act : stp._actions){
            if((act.cmd() ==  Action::JSON_CMD_SET)
                && (act.key() == valueKey)){
                scheduleStepEntry_t entry;
                
                entry.stepNo = stepNo;
                entry.value = act.value();
                entry.offset = offset;
                entries.push_back(entry);
            }
        }
        offset+=stp.duration();
      }
    
    if(entries.size() > 0){
        ssEntry = entries;
        found = true;
    }

    return found;
}


bool Sequence::allKeysInSequence(std::set<std::string> &keysfound){
    bool found = false;
 
    for(uint stepNo = 0; stepNo < _steps.size(); stepNo++){
        auto &stp =  _steps[stepNo];
        for(auto &act : stp._actions){
            if(act.cmd() ==  Action::JSON_CMD_SET)
                keysfound.insert(act.key());
        }
    }
    
    found = keysfound.size() > 0;
    
    return found;
}
