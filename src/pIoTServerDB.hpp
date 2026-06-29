//
//  pIoTServerDB.hpp
//
//  Created by Vincent Moscaritolo on 5/8/22.
//

#pragma once


#include <map>
#include <algorithm>
#include <mutex>
#include <bitset>
#include <strings.h>
#include <set>
#include <random>
#include <tuple>
#include <functional>
#include <vector>
#include <string>

#include <cstring>
#include <time.h>
#include <sqlite3.h>

#include "json.hpp"

#include "CommonDefs.hpp"
#include "pIoTServerMgrCommon.hpp"
#include "PropValKeys.hpp"
#include "pIoTServerSchema.hpp"

#include "EventAction.hpp"
#include "Sequence.hpp"
#include "Rule.hpp"

using namespace std;
using namespace nlohmann;


typedef  unsigned short sequenceGroupID_t;
bool str_to_SequenceGroupID(const char* str, sequenceGroupID_t *sequenceGroupIDOut = NULL);
string  SequenceGroupID_to_string(sequenceGroupID_t sequenceGroupID);

// dont change the order of these numbers, they persist in database
typedef enum {
    ALERT_UNKNOWN               = 0,
    ALERT_START                 = 1,
    ALERT_SHUTDOWN              = 2,
    ALERT_MESSAGE               = 3,
    ALERT_ERROR                 = 4,

}alert_t;

typedef struct {
    sequenceID_t    sid;
    EventTrigger    trigger;
    bool           enabled;
    vector<scheduleStepEntry_t> steps;
} sequenceScheduleEntry_t;

valueSchemaUnits_t schemaUnitsForString(string str);
string stringforSchemaUnits(valueSchemaUnits_t unit);
valueTracking_t trackingValueForString(string str);
string stringForTrackingValue(valueTracking_t tr);

class MinMaxValue {

public:
    MinMaxValue ();

    void setValue(time_t time, double value);

    bool getMax(double &value);
    bool getMin(double &value);
    time_t lastTime() { return _lastTime;};

private:
    void commonInit();

    time_t          _lastTime;

     struct  {
         double      minValue;
          double      maxValue;
     } _entries[24];
};


class pIoTServerDB  {

    public:

    typedef struct {
        string                      title;
        valueSchemaUnits_t          units;
        valueTracking_t             tracking;
        bool                        readOnly;       // only the device can set this value..
    } valueSchema_t;

    typedef struct {
        string                      key;
        double                      minValue;
        double                      maxValue;
   } minMaxEntry_t;

    pIoTServerDB ();
    ~pIoTServerDB ();

    bool initLogDatabase(string assetPath);
    void clearValues();

    typedef tuple<time_t, alert_t, string>  historicAlertEntry_t;
    typedef vector<historicAlertEntry_t>    historicAlerts_t;

    typedef vector<pair<time_t, string>> historicValues_t;

    typedef vector<tuple <time_t, double, double >  >historicRanges_t;

    // MARK: - properties // persistent
    bool savePropertiesToFile(string fileName, string assetDirPath) ;
    bool saveProperties();

    bool restorePropertiesFromFile(string fileName, string assetDirPath);

    bool setProperty(string key, string value);
    bool setProperty(string key, nlohmann::json  j);
    bool getProperty(string key, string *value);

    bool setPropertyIfNone(string key, string value);

    bool getUint16Property(string key, uint16_t * value);
    bool getTimeProperty(string key, time_t * value);
    bool getFloatProperty(string key, float * valOut);
    bool getBoolProperty(string key, bool * valOut);
    bool getIntProperty(string key, int * value);
    bool getJSONProperty(string key, nlohmann::json  *j);

    bool getAllProperties(vector<string_view> filter, nlohmann::json  *j);

    bool removeProperty(string key);
    vector<string> propertiesKeys();

    bool propertiesChanged() {return _didChangeProperties;};

    // MARK  - Config properties
    bool getConfigProperty(string key, string &value);
    bool setConfigProperty(string key, string value);
    bool removeConfigProperty(string key);


    // MARK: -  API Secrets
    bool apiSecretCreate(string APIkey, string APISecret);
    bool apiSecretDelete(string APIkey);
    bool apiSecretSetSecret(string APIkey, string APISecret);
    bool apiSecretGetSecret(string APIkey, string &APISecret);
    bool apiSecretMustAuthenticate();

    // MARK: -   SERVER PORTS
    void  setRESTPort(int port);
    int   getRESTPort();

    // MARK: - values
    bool insertValue(string key, string value);
    bool insertValue(string key, string value, time_t when,  eTag_t eTag);
    bool insertValues(map<string,string>  values, time_t when = 0);

    bool isKeyInDB(string key);

    vector<string> keysChangedSinceEtag( eTag_t eTag);

    string displayStringForValue(string key, string value);
    void dumpMap();

    json    currentValuesJSON(eTag_t  eTag = 0);
    json    jsonForValue(string key, string value);
    json    currentJSONForKey(string key);
    json    currentJSONForKeys(stringvector keys);

    //  prevent schedules from changing a boolean value
    bool    setKeyManualMode(string key, bool manual);
    bool    isKeyInManualMode(string key);
    stringvector keysInManualMode();

    eTag_t  lastEtag() { return  _eTag;};
    eTag_t  nextEtag() { return  _eTag++;};

    bool    historyForKey(string key, historicValues_t &values,
                          int days = 0, int limit = 0, int offset = 0);

    bool    countHistoryForKey(string key, int &count);

    bool    removeHistoryForKey(string key, float days);

    bool    historyForRange(string key, historicRanges_t &history,
                          int days = 0, int limit = 0, int offset = 0);


    bool    countHistoryForRange(string key, int &count);

    bool    removeHistoryForRange(string key, float days);

    bool    getMinMaxForValues(stringvector keys, double hours,  vector<minMaxEntry_t> &entries);

    void    addSchema(string key,
                      valueSchemaUnits_t units,
                      string title,
                      valueTracking_t tracking,
                      bool            readOnly = false);

    json    schemaJSON();
    json    schemaJSON(string key);
    valueSchema_t schemaForKey(string key);
    valueSchemaUnits_t unitsForKey(string key);
    string      unitSuffixForKey(string key);

    static bool     isUnitNumeric(valueSchemaUnits_t unit);
    static string   normalizeStringForUnit(string val,  valueSchemaUnits_t unit);

    double      normalizedDoubleForValue(string key, string value);
    int         intForValue(string key, string value);

    bool        isValidDataTypeForKey(string key, string value);

    json        scheduleForValue(string valueKey);

    // MARK: - Value Snapshot for tinyexpr evaluator


    typedef struct numericValueSnapshot_t {
        string  name;
        bool    readOnly;
        bool    wasUpdated;
        double  value;
        double  mirrorValue;
     } numericValueSnapshot_t;

    bool      createValueSnapshot(vector<numericValueSnapshot_t> *snapshot,
                                  keyValueMap_t kv = {});


    // MARK: -  Rule
      bool ruleIDIsValid(ruleID_t rid);
      bool ruleFind(string name, ruleID_t &rid);
      bool ruleDelete(ruleID_t rid);
      bool ruleSave(Rule rule, ruleID_t* ridOut = NULL);
      bool ruleUpdate(ruleID_t rid, Rule rule);
      string ruleGetName(ruleID_t rid);
      bool ruleSetName(ruleID_t rid, string name);
      bool ruleSetDescription(ruleID_t rid, string desc);
      bool ruleSetEnable(ruleID_t rid, bool enable);
      bool ruleIsEnabled(ruleID_t rid);

      bool ruleIsActive(ruleID_t rid);
      bool ruleSetActive(ruleID_t rid, bool active);

      string ruleGetCondition(ruleID_t rid);

      string ruleGetClearCondition(ruleID_t rid);

      uint64_t ruleGetTriggerDelay(ruleID_t rid);
      uint64_t ruleGetClearDelay(ruleID_t rid);

      time_t ruleGetConditionTrueSince(ruleID_t rid);
      bool ruleSetConditionTrueSince(ruleID_t rid, time_t t);

      time_t ruleGetClearTrueSince(ruleID_t rid);
      bool ruleSetClearTrueSince(ruleID_t rid, time_t t);

      bool ruleSetLastActionTime(ruleID_t rid, time_t t);
      bool ruleSetLastClearActionTime(ruleID_t rid, time_t t);

      bool ruleGetActions(ruleID_t rid, vector<Action>& actions);
      bool ruleGetClearActions(ruleID_t rid, vector<Action>& actions);

      bool triggerRule(ruleID_t rid);

      json ruleJSON(ruleID_t rid);
      vector<ruleID_t> allruleIDs();



    //
    // MARK: -  Sequence
    bool sequenceIDIsValid(sequenceID_t sequenceID);
    bool sequenceFind(string name, sequenceID_t &sid);
    bool sequenceDelete(sequenceID_t sid);
    bool sequenceSave(Sequence seq, sequenceID_t* sidOut = NULL);
    bool sequenceUpdate(sequenceID_t sid, Sequence sequence);
    string sequenceGetName(sequenceID_t sid);
    bool sequenceSetName(sequenceID_t sid, string name);
    bool sequenceSetDescription(sequenceID_t sid, string desc);
    bool sequenceSetEnable(sequenceID_t sid, bool enable);
    bool sequenceisEnable(sequenceID_t sid);
    bool sequenceShouldIgnoreLog(sequenceID_t sid);
    bool sequenceShouldIgnoreManualMode(sequenceID_t sid);
    bool sequenceIsRunning(sequenceID_t sid);
    bool sequenceIsEphemeral(sequenceID_t sid);

    bool triggerSequence(sequenceID_t sid);
    string sequenceGetCondition(sequenceID_t sid);

    bool sequenceGetTrigger(sequenceID_t sid, EventTrigger &trig);
    json sequenceJSON(sequenceID_t sid);

    vector<sequenceID_t> allSequenceIDs();

    vector<sequenceID_t> sequencesMatchingAppEvent(EventTrigger::app_event_t appEvent);
    vector<sequenceID_t> sequencesThatNeedToRunNow(solarTimes_t &solar, time_t localNow);

    vector<sequenceID_t> sequencesInTheFuture(solarTimes_t &solar, time_t localNow);
    vector<sequenceID_t> sequencesCron();

    bool sequenceStepsCount(sequenceID_t sid, uint &count);
    bool sequenceStepsDuration(sequenceID_t sid, uint64_t &duration);

    bool sequenceNextStepNumberToRun(sequenceID_t sid, uint &stepNo);  // true == more steps
    bool sequenceGetStep(sequenceID_t sid, uint stepNo, Step &step);
    bool sequenceCompletedStep(sequenceID_t sid, uint stepNo, time_t time); // true == more steps
    uint64_t sequenceStepDuration(sequenceID_t sid, uint stepNo);

    bool sequenceGetAbortActions(sequenceID_t sid, vector<Action> & act);

    bool sequenceStartAbort(sequenceID_t sid);
    bool sequenceIsAborting(sequenceID_t sid);

    bool sequenceReset(sequenceID_t sid);       // reset the step count

    bool sequenceSetRunning(sequenceID_t sid, bool isrunning);

    bool sequenceSetLastRunTime(sequenceID_t sid,time_t localNow);

    bool sequenceSetCurrentStep(sequenceID_t sid, uint stepNo); // true == more steps
    bool sequenceCurrentStep(sequenceID_t sid, uint &stepNo);

    bool sequenceEvaluateCondition(sequenceID_t sid);

    const std::string sequencePrintString(sequenceID_t sid);

    bool allKeysInSequence(sequenceID_t sid, std::set<std::string> &keysfound, bool onlyEnabled = true);

    bool allKeysInAllSequences(std::set<std::string> &keysfound, bool onlyEnabled = true);

    // MARK: -  sequence groups
    bool sequenceGroupIsValid(sequenceGroupID_t seqGroupID);
    bool sequenceGroupCreate(sequenceGroupID_t* seqGroupID, const string name);
    bool sequenceGroupDelete(sequenceGroupID_t seqGroupID);
    bool sequenceGroupFind(string name, sequenceGroupID_t* seqGroupID);
    bool sequenceGroupSetName(sequenceGroupID_t seqGroupID, string name);
    string sequenceGroupGetName(sequenceGroupID_t seqGroupID);
    bool sequenceGroupAddSequence(sequenceGroupID_t seqGroupID,  sequenceID_t sid);
    bool sequenceGroupRemoveSequence(sequenceGroupID_t seqGroupID, sequenceID_t sid);
    bool sequenceGroupContainsSequenceID(sequenceGroupID_t seqGroupID, sequenceID_t sid);
    vector<sequenceID_t> sequenceGroupGetSequenceIDs(sequenceGroupID_t seqGroupID);
    vector<sequenceGroupID_t> allSequenceGroupIDs();
    void reconcileSequenceGroup(const solarTimes_t &solar, time_t localNow);

    // IncidentMgr functions
    bool initIncidentTables();

    bool noticeIncident(const std::string& source,
                        const std::string& code,
                        const std::string& key,
                        const char* message = nullptr,
                        const char* details = nullptr);

    bool raiseIncident(const std::string& source,
                       int severity,
                       const std::string& code,
                       const std::string& key,
                       const char* message = nullptr,
                       const char* details = nullptr);

    bool clearIncident(const std::string& source,
                       const std::string& code,
                       const std::string& key,
                       const char* message = nullptr,
                       const char* details = nullptr);

    typedef std::tuple<int64_t, std::string, std::string, std::string, int, bool, time_t, time_t, time_t, int, int64_t, std::string, std::string> historicIncident_t;
    typedef std::vector<historicIncident_t> historicIncidents_t;

    bool incidentGetEtag(int64_t &etagOut);

    bool historyForIncidents(historicIncidents_t &incidentsOut,
                             float days = 0,
                             int limit = 0,
                             int offset = 0,
                             bool activeOnly = false,
                             int64_t sinceEtag = 0);

    bool countHistoryForIncidents(int &countOut,
                                  bool activeOnly = false);

    bool removeHistoryForIncidents(float days = 0,
                                   bool inactiveOnly = true);

    bool removeHistoryBeforeLastIncidentStart(bool inactiveOnly = true);

    bool removeAllIncidents();

    // MARK: - utility

    string makeNonce();

 private:

    mutable std::mutex                _mutex;
    nlohmann::json                    _props;
    sqlite3*                           _sdb;

    string                             _propertyFilePath;
    bool                                _didChangeProperties;

    // value database

    map<string, pair<time_t, string>>   _values;
    map<string,valueSchemaUnits_t>      _schemaMap;
    map<string, valueSchema_t>          _schema;

    set<string>                        _keysInManualMode;

    map<string, eTag_t>                 _etagMap;
    eTag_t                              _eTag;        // last change tag

    mt19937                        _rng;

    map<ruleID_t, Rule>            _rules;

    map<sequenceID_t, Sequence>   _sequences;

    typedef struct {
        string name;
        set<sequenceID_t>  sequenceIDs;
    } sequenceGroupInfo_t;

    map<sequenceGroupID_t, sequenceGroupInfo_t>   _sequenceGroups;

    map< string, MinMaxValue> _minMaxValues;

    // MARK: - Database Ops
    bool restoreValuesFromDB();
    bool insertValueToDB(string key, string value, time_t time );
    bool saveUniqueValueToDB(string key, string value, time_t time );
    bool valueShouldUpdate(string key, string value);

    bool insertRangeToDB(string key, double minVal, double maxVal, time_t time );

    string defaultPropertyFileName();
    bool  refreshSolarEvents();

    sequenceID_t     createUniqueSequenceID();
    sequenceGroupID_t  createUniqueSequenceGroupID();
    ruleID_t     createUniqueRuleID();

    bool restoreSequenceGroupFromJSON(json j);
    bool saveSequenceGroupToJSON(sequenceGroupID_t, json &j );

    bool sequencesEffectingValue(string valueKey,
                                 vector<sequenceScheduleEntry_t> &sse,
                                 bool onlyEnabled = true);

    bool snapshotForKV(string key, string value, numericValueSnapshot_t *ss);

    bool currentValueDouble(string key, double &val);

    bool canMinMaxForUnit(valueSchemaUnits_t unit);

    void setupDatabasePeriodicTasks();
    void runDailyTask();

    bool nextIncidentEtagLocked(int64_t &etagOut);
 };
