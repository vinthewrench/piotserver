//
//  pIoTServerDB_Sequences.cpp
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

#define DBL_MAX std::numeric_limits<double>::max()
#define TIME_MAX    std::numeric_limits<time_t>::max()

// MARK: - Sequence API

sequenceID_t pIoTServerDB::createUniqueSequenceID(){
    std::uniform_int_distribution<long> distribution(SHRT_MIN,SHRT_MAX);
    sequenceID_t sid;
    do {
        sid = distribution(_rng);
    }while( _sequences.count(sid) > 0);

    return sid;
}

bool pIoTServerDB::sequenceIDIsValid(sequenceID_t sid){
    return(_sequences.count(sid) > 0);
}

bool pIoTServerDB::sequenceFind(string name, sequenceID_t &sidOut){
    std::lock_guard<std::mutex> lock(_mutex);

    for(auto e : _sequences) {
        auto seq = &e.second;

        if (strcasecmp(name.c_str(), seq->_name.c_str()) == 0){
            sidOut = e.first;
            return true;
        }
    }
    return false;
}

vector<sequenceID_t> pIoTServerDB::allSequenceIDs(){
    std::lock_guard<std::mutex> lock(_mutex);

    vector<sequenceID_t> sids;

    for (const auto& [key, _] : _sequences) {
        sids.push_back( key);
    }

    return sids;
}

 string pIoTServerDB::sequenceGetCondition(sequenceID_t sid) {
    std::lock_guard<std::mutex> lock(_mutex);

    if(_sequences.count(sid) == 0)
        return string();

    Sequence* seq =  &_sequences[sid];
     return seq->_condition;
}

string pIoTServerDB::sequenceGetName(sequenceID_t sid) {
    std::lock_guard<std::mutex> lock(_mutex);

    if(_sequences.count(sid) == 0)
        return string();

    Sequence* seq =  &_sequences[sid];
    return seq->_name;
}

bool pIoTServerDB::sequenceSetName(sequenceID_t sid, string name){
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(_sequences.count(sid) == 0)
            return false;

        Sequence* seq =  &_sequences[sid];
        seq->_name = name;
    }
    saveProperties();
    return true;
}

bool pIoTServerDB::sequenceSetDescription(sequenceID_t sid, string desc){
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(_sequences.count(sid) == 0)
            return false;

        Sequence* seq =  &_sequences[sid];
        seq->_description = desc;
    }
    saveProperties();
    return true;
}

bool pIoTServerDB::sequenceSetEnable(sequenceID_t sid, bool enable){
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(_sequences.count(sid) == 0)
            return false;

        Sequence* seq =  &_sequences[sid];
        seq->_enable = enable;
    }

    saveProperties();
    return true;
}

bool pIoTServerDB::sequenceisEnable(sequenceID_t sid){
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(_sequences.count(sid) == 0)
            return false;

        Sequence* seq =  &_sequences[sid];
        return (seq->_enable);
    }
      return false;
}

bool pIoTServerDB::sequenceIsRunning(sequenceID_t sid){
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(_sequences.count(sid) == 0)
            return false;

        Sequence* seq =  &_sequences[sid];
        return (seq->_isRunning);
    }
      return false;
}

bool pIoTServerDB::sequenceShouldIgnoreManualMode(sequenceID_t sid){
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(_sequences.count(sid) == 0)
            return false;

        Sequence* seq =  &_sequences[sid];
        return (seq->_overrideManualMode);
    }
      return false;
}

bool pIoTServerDB::sequenceShouldIgnoreLog(sequenceID_t sid){
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(_sequences.count(sid) == 0)
            return false;

        Sequence* seq =  &_sequences[sid];
        return (seq->_dontLog);
    }
      return false;
}

bool pIoTServerDB::sequenceSave(Sequence seq, sequenceID_t* sidOut){
    sequenceID_t sid;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        sid = createUniqueSequenceID();
        seq._rawSequenceID = sid;
        _sequences[sid] = seq;
    }

    saveProperties();

    if(sidOut)
        *sidOut = sid;

    return true;
}

bool pIoTServerDB::sequenceDelete(sequenceID_t sid){
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(_sequences.count(sid) == 0)
            return false;

        _sequences.erase(sid);
    }
    saveProperties();
    return true;
}

bool pIoTServerDB::sequenceUpdate(sequenceID_t sid, Sequence newSequence){
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if(_sequences.count(sid) == 0)
            return false;

        Sequence* seq =  &_sequences[sid];

        if(!newSequence._name.empty())
            seq->_name = newSequence._name;

        if(!newSequence._description.empty())
            seq->_description = newSequence._description;

        if( newSequence._trigger.isValid())
            seq->_trigger = newSequence._trigger;

        seq->_steps = newSequence._steps;
        seq->_enable = newSequence._enable;
    }
    saveProperties();
    return true;
}

bool  pIoTServerDB::sequenceStepsCount(sequenceID_t sid, uint &count){
    if(_sequences.count(sid) >0 ){
        auto seq = _sequences[sid];
        count =  seq.stepsCount();
          return true;
    }
    return false;
}

bool  pIoTServerDB::sequenceStepsDuration(sequenceID_t sid, uint64_t &duration){
    if(_sequences.count(sid) >0 ){
        auto seq = _sequences[sid];
        duration =  seq.stepsDuration();
          return true;
    }
    return false;
}

bool  pIoTServerDB::sequenceNextStepNumberToRun(sequenceID_t sid, uint &stepNo){
    std::lock_guard<std::mutex> lock(_mutex);

    if(_sequences.count(sid) >0 ){
       auto seq = _sequences[sid];
        if(seq._nextStepToRun < seq.stepsCount()){
            stepNo = seq._nextStepToRun;
            return true;
        }
     }
    return false;
}

bool  pIoTServerDB::sequenceGetStep(sequenceID_t sid, uint stepNo, Step &step){
    if(_sequences.count(sid) >0 ){
        auto seq = _sequences[sid];
        if(stepNo < seq.stepsCount()){
            step = seq._steps[stepNo];
            return true;
        }
      }
    return false;
}

bool pIoTServerDB::sequenceSetCurrentStep(sequenceID_t sid, uint stepNo){
    std::lock_guard<std::mutex> lock(_mutex);

    if(_sequences.count(sid) >0 ){
        auto seq = &_sequences[sid];
        seq->_currentStepNumber = stepNo;
        return true;
      }
    return false;
}

bool  pIoTServerDB::sequenceCurrentStep(sequenceID_t sid, uint &stepNo){
    std::lock_guard<std::mutex> lock(_mutex);

    if(_sequences.count(sid) >0 ){
       auto seq = _sequences[sid];
        if(seq._currentStepNumber  != UINT_MAX){
            stepNo = seq._currentStepNumber;
            return true;
        }
     }
    return false;
}

bool pIoTServerDB::sequenceCompletedStep(sequenceID_t sid, uint stepNo, time_t time){
    std::lock_guard<std::mutex> lock(_mutex);

    if(_sequences.count(sid) >0 ){
        auto seq = &_sequences[sid];
        seq->_nextStepToRun = stepNo+1;
        seq->_lastStepRunTime = time;

        if(seq->_nextStepToRun < seq->stepsCount()){
            return true;
        }
        else
        {
            seq->_nextStepToRun = 0;
            seq->_wasManuallyTriggered = false;
            return false;
        }
      }
    return false;
}

bool pIoTServerDB::sequenceGetTrigger(sequenceID_t sid, EventTrigger &trig){
    std::lock_guard<std::mutex> lock(_mutex);

    if(_sequences.count(sid) == 0)
        return false;

    trig = _sequences[sid]._trigger;
    return true;
 }

 bool pIoTServerDB::sequenceIsEphemeral(sequenceID_t sid){
     std::lock_guard<std::mutex> lock(_mutex);

     if(_sequences.count(sid) == 0)
         return false;

     Sequence* seq =  &_sequences[sid];
     return seq->isEphemeral();
 }

bool pIoTServerDB::triggerSequence(sequenceID_t sid){
    if(_sequences.count(sid) == 0)
        return false;

    if(_sequences[sid].wasManuallyTriggered())
        return false;

    _sequences[sid].setManuallyTriggered(true);

    return true;
}

vector<sequenceID_t> pIoTServerDB::sequencesMatchingAppEvent(EventTrigger::app_event_t appEvent){
    std::lock_guard<std::mutex> lock(_mutex);

    vector<sequenceID_t> items;

    for (auto& [key, seq] : _sequences) {
        if( seq.isEnabled()) {

            int stepNo;
            if(seq.shouldRunSequenceFromAppEvent(appEvent, stepNo)){
                items.push_back(key);
             }
        }
    };

    return items;
}

uint64_t pIoTServerDB::sequenceStepDuration(sequenceID_t sid, uint stepNo){
    uint64_t duration = 0;
    std::lock_guard<std::mutex> lock(_mutex);
    if(_sequences.count(sid)){
        Sequence* seq =  &_sequences[sid];
        Step step;
        if( seq->getStep(stepNo, step)){
            duration = step.duration();
        }
     }

    return duration;
}

vector<sequenceID_t> pIoTServerDB::sequencesThatNeedToRunNow(solarTimes_t &solar, time_t localNow){
    std::lock_guard<std::mutex> lock(_mutex);

    vector<sequenceID_t> sid;

    for (auto& [key, seq] : _sequences) {

        if( seq.isEnabled()) {
            if(seq._trigger.shouldTriggerFromTimeEvent(solar, localNow)
               || seq.wasManuallyTriggered()){

                if(seq._nextStepToRun  == 0){
                    sid.push_back( key);
                }
                else {
                    Step lastStepRun;
                    seq.getStep(seq._nextStepToRun -1, lastStepRun);
                    uint64_t lastDuration = lastStepRun.duration();

                    if(seq._lastStepRunTime == 0){
                        sid.push_back( key);
                    }
                    else if(localNow > seq._lastStepRunTime){
                        const auto elapsed = static_cast<uint64_t>(localNow - seq._lastStepRunTime);

                        if(elapsed > lastDuration){
                            sid.push_back( key);
                        }
                    }
                 }
            }
         }
    };

    return sid;
}

bool pIoTServerDB::sequenceSetLastRunTime(sequenceID_t sid,time_t localNow){
    std::lock_guard<std::mutex> lock(_mutex);

    bool result = false;

    if(_sequences.count(sid) > 0){
        Sequence* seq =  &_sequences[sid];

        if(seq->_trigger.isTimed()){
            result = seq->_trigger.setLastRun(localNow);
        }
        else if(seq->_trigger.isCronEvent()){
            result = seq->_trigger.scheduleNextCronTime();
        }
        else if(seq->_trigger.isAppEvent()){
            result = true;
        }
    }
    return result;
}

bool pIoTServerDB::sequenceEvaluateCondition(sequenceID_t sid){
    bool result  = true;

    if(_sequences.count(sid) > 0){
        Sequence* seq =  &_sequences[sid];

        if(seq->hasCondition()){
            vector<numericValueSnapshot_t> vars = {};

            if( createValueSnapshot(&vars)){
                double val = 0;
                if(evaluateExpression(seq->condition(), vars, val)) {
                    result = val != 0;
                }
            }
        }
    }
    return result;
}

bool pIoTServerDB::sequenceReset(sequenceID_t sid) {
    std::lock_guard<std::mutex> lock(_mutex);

    if(_sequences.count(sid) > 0){
        Sequence* seq =  &_sequences[sid];
        seq->_nextStepToRun = 0;
        seq->_wasManuallyTriggered = false;
        return true;
    };

    return false;
}

bool pIoTServerDB::sequenceSetRunning(sequenceID_t sid, bool isrunning){
    std::lock_guard<std::mutex> lock(_mutex);

    if(_sequences.count(sid) > 0){
        Sequence* seq =  &_sequences[sid];
        seq->_isRunning = isrunning;
       return true;
    };

    return false;
}

bool pIoTServerDB::sequenceStartAbort(sequenceID_t sid){
    std::lock_guard<std::mutex> lock(_mutex);

    if(_sequences.count(sid) > 0){
        Sequence* seq =  &_sequences[sid];
        seq->_nextStepToRun = UINT_MAX;
        return true;
    };

    return false;
}

bool pIoTServerDB::sequenceIsAborting(sequenceID_t sid){
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(_sequences.count(sid) == 0)
            return false;

        Sequence* seq =  &_sequences[sid];
        return (  seq->_isRunning  && (seq->_nextStepToRun == UINT_MAX));
    }
      return false;
}

bool  pIoTServerDB::sequenceGetAbortActions(sequenceID_t sid, vector<Action> & act){
    bool result  = false;

    if(_sequences.count(sid) > 0){
        Sequence* seq =  &_sequences[sid];
        result = seq->getAbortActions(act);
    }
    return result;
}

json pIoTServerDB::sequenceJSON(sequenceID_t sid){
    json j;

    if(_sequences.count(sid)){
        j = _sequences[sid].JSON();
    }
    return j;
}

vector<sequenceID_t> pIoTServerDB::sequencesInTheFuture(solarTimes_t &solar, time_t localNow){
    std::lock_guard<std::mutex> lock(_mutex);

    vector<sequenceID_t> sids;

    for (auto& [key, seq] : _sequences) {
        if( seq.isEnabled() && seq._trigger.shouldTriggerInFuture(solar, localNow))
            sids.push_back( key);
    };

    return sids;
}

vector<sequenceID_t> pIoTServerDB::sequencesCron(){
   std::lock_guard<std::mutex> lock(_mutex);

    vector<sequenceID_t> sids;

   for (auto& [key, seq] : _sequences) {
       if(seq.isEnabled()  && seq._trigger.isCronEvent())
           sids.push_back(key);
   };

   return sids;
}

const std::string pIoTServerDB::sequencePrintString(sequenceID_t sid){
    string str = "<invalid>";
    if(_sequences.count(sid) > 0){
        Sequence* seq =  &_sequences[sid];
        str = seq->printString();
    }

    return str;
}

bool pIoTServerDB::sequencesEffectingValue(string valueKey,
                                           vector<sequenceScheduleEntry_t> &sse,
                                            bool onlyEnabled) {
    bool found = false;

    vector<sequenceScheduleEntry_t> entries;
    for(auto [sid,seq]: _sequences){

        if(onlyEnabled && !seq.isEnabled()) continue;
        if(seq.isAppEvent()) continue;

        vector<scheduleStepEntry_t> ssEntry;
        if(seq.scheduleForValue(valueKey, ssEntry)){

            sequenceScheduleEntry_t entry;
            entry.sid = sid;
            entry.trigger = seq._trigger;
            entry.enabled = seq._enable;
            entry.steps = ssEntry;

            entries.push_back(entry);
        }
    }

    if(entries.size() > 0){
        sse = entries;
        found = true;
    }

    return found;
}

bool pIoTServerDB::allKeysInSequence(sequenceID_t sid, std::set<std::string> &keysfound,
                                     bool onlyEnabled ){
    bool found = false;

    if(_sequences.count(sid) > 0){
        Sequence* seq =  &_sequences[sid];
        if(onlyEnabled && seq->isEnabled())
            found = seq->allKeysInSequence(keysfound);
    }

    return found;
}

bool pIoTServerDB::allKeysInAllSequences(std::set<std::string> &keysfound, bool onlyEnabled){
    bool found = false;

    for (auto& [key, seq] : _sequences) {

        if(onlyEnabled && !seq.isEnabled()) continue;
        if(seq.isAppEvent()) continue;

        set<string> keys;
        keys.clear();
        seq.allKeysInSequence(keys);
        keysfound.insert(keys.begin(), keys.end());
    }

    found = keysfound.size()>0;
    return found;
}

json pIoTServerDB::scheduleForValue(string valueKey){
    json schedule;

      solarTimes_t solar;
    SolarTimeMgr::shared()->getSolarEventTimes(solar);

    time_t lastMidnight = solar.previousMidnight  - solar.gmtOffset;

    vector<sequenceScheduleEntry_t> sse;
    if(sequencesEffectingValue(valueKey,sse)){
        for(auto &entry: sse){
            bool isCron = false;

            time_t actual_time = 0;
            EventTrigger &trig = entry.trigger;
            string trigStr = trig.printString(false);

            if(solar.isValid && trig.isTimed()){

                int16_t minsFromMidnight = 0;
                if(trig.calculateTriggerTime(solar,minsFromMidnight)) {
                    actual_time  = lastMidnight + (minsFromMidnight  *60);
                   }
             }
            else if(trig.isCronEvent()){
                isCron = trig.nextCronTime(actual_time);
             }

            if(actual_time != 0){
                for(auto step :entry.steps){
                    json se;
                    se[JSON_EVENT_TRIGGER_STRING] = trigStr;
                    se[JSON_ARG_VALUE] =  step.value;
                    se[JSON_ARG_TIME] = actual_time + step.offset;
                    se[JSON_TIME_OFFSET] = step.offset;
                    se[JSON_ARG_STEP]  = step.stepNo;
                    se[JSON_ARG_SEQUENCE_ID] =  SequenceID_to_string(entry.sid);
                    se[JSON_ARG_ENABLE] = entry.enabled;
                    se[JSON_ARG_CRON] = isCron;
                    schedule.push_back(se);
                   }
            }
        }
    }

    return schedule;
}

// MARK: -  sequence groups

bool str_to_SequenceGroupID(const char* str, sequenceGroupID_t *sequenceGroupIDOut){
    bool status = false;

    sequenceGroupID_t val = 0;
    status = sscanf(str, "%hx", &val) == 1;

    if(sequenceGroupIDOut)  {
        *sequenceGroupIDOut = val;
    }

    return status;
};

string  SequenceGroupID_to_string(sequenceGroupID_t sequenceGroupID){
    return to_hex<unsigned short>(sequenceGroupID);
}

bool pIoTServerDB::sequenceGroupIsValid(sequenceGroupID_t seqGroupID){
    return (_sequenceGroups.count(seqGroupID) > 0);
}

sequenceGroupID_t pIoTServerDB::createUniqueSequenceGroupID(){
    std::uniform_int_distribution<long> distribution(SHRT_MIN,SHRT_MAX);
    sequenceGroupID_t sgid;
    do {
        sgid = distribution(_rng);
    }while( _sequenceGroups.count(sgid) > 0);

    return sgid;
}

bool pIoTServerDB::sequenceGroupCreate(sequenceGroupID_t* sgidOUT, const string name){
    sequenceGroupID_t sgid;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        sgid = createUniqueSequenceGroupID();

        sequenceGroupInfo_t info;
        info.name = name;
        info.sequenceIDs.clear();
        _sequenceGroups[sgid] = info;
    }

    saveProperties();

    if(sgidOUT)
        *sgidOUT = sgid;
    return true;
}

bool pIoTServerDB::sequenceGroupDelete(sequenceGroupID_t sgid){
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(_sequenceGroups.count(sgid) == 0)
            return false;

        _sequenceGroups.erase(sgid);
    }
    saveProperties();

    return true;
}

bool pIoTServerDB::sequenceGroupFind(string name, sequenceGroupID_t* sgidOUT){
    for(auto g : _sequenceGroups) {
        auto info = &g.second;

        if (strcasecmp(name.c_str(), info->name.c_str()) == 0){
            if(sgidOUT){
                *sgidOUT =  g.first;
            }
            return true;
        }
    }
    return false;
}

bool pIoTServerDB::sequenceGroupSetName(sequenceGroupID_t seqGroupID, string name){
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(_sequenceGroups.count(seqGroupID) == 0)
            return false;

        sequenceGroupInfo_t* info  =  &_sequenceGroups[seqGroupID];
        info->name = name;
    }
    saveProperties();
    return true;
}

string pIoTServerDB::sequenceGroupGetName(sequenceGroupID_t seqGroupID){
    if(_sequenceGroups.count(seqGroupID) == 0)
        return "";

    sequenceGroupInfo_t* info  =  &_sequenceGroups[seqGroupID];
    return info->name;
}

bool pIoTServerDB::sequenceGroupAddSequence(sequenceGroupID_t seqGroupID,  sequenceID_t sid){
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(_sequenceGroups.count(seqGroupID) == 0)
            return false;

        if(!sequenceIDIsValid(sid))
            return false;

        sequenceGroupInfo_t* info  =  &_sequenceGroups[seqGroupID];
        info->sequenceIDs.insert(sid);
    }
    saveProperties();

    return true;
}

bool pIoTServerDB::sequenceGroupRemoveSequence(sequenceGroupID_t seqGroupID, sequenceID_t sid){
    {
        std::lock_guard<std::mutex> lock(_mutex);

        if(_sequenceGroups.count(seqGroupID) == 0)
            return false;

        sequenceGroupInfo_t* info  =  &_sequenceGroups[seqGroupID];

        if(info->sequenceIDs.count(sid) == 0)
            return false;

        info->sequenceIDs.erase(sid);
    }
    saveProperties();
    return true;
}

bool pIoTServerDB::sequenceGroupContainsSequenceID(sequenceGroupID_t seqGroupID, sequenceID_t sid){
    if(_sequenceGroups.count(seqGroupID) == 0)
        return false;

    sequenceGroupInfo_t* info  =  &_sequenceGroups[seqGroupID];

    return(info->sequenceIDs.count(sid) != 0);
}

vector<sequenceID_t> pIoTServerDB::sequenceGroupGetSequenceIDs(sequenceGroupID_t sid){
    vector<sequenceID_t> sids;

    if(_sequenceGroups.count(sid) != 0){
        sequenceGroupInfo_t* info  =  &_sequenceGroups[sid];
        std::copy(info->sequenceIDs.begin(), info->sequenceIDs.end(), std::back_inserter(sids));
    }

    return sids;
}

vector<sequenceGroupID_t> pIoTServerDB::allSequenceGroupIDs(){
    vector<sequenceID_t> sids;

    for (const auto& [key, _] : _sequenceGroups) {
        sids.push_back( key);
    }

    return sids;
}

void pIoTServerDB::reconcileSequenceGroup(const solarTimes_t &solar, time_t localNow){
    long nowMins = (localNow - solar.previousMidnight) / SECS_PER_MIN;

    for (const auto& [sgid, _] : _sequenceGroups) {
        sequenceGroupInfo_t* info  =  &_sequenceGroups[sgid];

        map <int16_t, sequenceID_t> seqMap;

        for(auto sid : info->sequenceIDs ){
            Sequence* seq =  &_sequences[sid];
            if(!seq->isEnabled()) continue;

            int16_t minsFromMidnight = 0;

            if(seq->_trigger.calculateTriggerTime(solar,minsFromMidnight)){
                if(minsFromMidnight <= nowMins){
                    seqMap[minsFromMidnight] = sid;
                }
            }
        };

        if(seqMap.size() > 0){
            seqMap.erase(prev(seqMap.end()));

            for(auto item : seqMap ){
                auto sid  = item.second;
                sequenceSetLastRunTime(sid, localNow);
            }
         }
    }
 }

bool pIoTServerDB::restoreSequenceGroupFromJSON(json j){
    bool  statusOk = false;

    if(j.is_object()){
        if( j.contains(string(PROP_ARG_GROUPID))
           && j.at(string(PROP_ARG_GROUPID)).is_string()){
            string sgid = j.at(string(PROP_ARG_GROUPID));
            sequenceGroupID_t sequenceGroupID;

            if( str_to_SequenceGroupID(sgid.c_str(), &sequenceGroupID )
               && !sequenceGroupIsValid(sequenceGroupID)){

                sequenceGroupInfo_t info;
                info.sequenceIDs.clear();

                if( j.contains(string(JSON_ARG_NAME))
                   && j.at(string(JSON_ARG_NAME)).is_string()){
                    info.name = j.at(string(JSON_ARG_NAME));
                }

                if( j.contains(string(JSON_ARG_SEQUENCE_IDS))
                   && j.at(string(JSON_ARG_SEQUENCE_IDS)).is_array()){
                    auto sids = j.at(string(JSON_ARG_SEQUENCE_IDS));
                    for(string str : sids){
                        sequenceID_t sid;
                        if( str_to_SequenceID(str.c_str(), &sid )){
                            info.sequenceIDs.insert(sid);
                        }
                    }
                }

                _sequenceGroups[sequenceGroupID] = info;
                statusOk = true;
            }
        }
    }
    return statusOk;
};

bool pIoTServerDB::saveSequenceGroupToJSON(sequenceGroupID_t sgid, json &j ){
    bool  statusOk = false;

    if(sequenceGroupIsValid(sgid)){
        sequenceGroupInfo_t* sg =  &_sequenceGroups[sgid];

        j[string(PROP_ARG_GROUPID)] = to_hex<unsigned short>(sgid);
        if(!sg->name.empty()) j[string(JSON_ARG_NAME)] =  sg->name;
        vector<string> sids;
        sids.clear();
        for(auto e : sg->sequenceIDs) sids.push_back( SequenceID_to_string(e));
        j[string(JSON_ARG_SEQUENCE_IDS)]  = sids;
        statusOk = true;
    }

    return statusOk;
}
