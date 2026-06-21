//
//  pIoTServerDB_Values.cpp
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

// MARK: - values

void  pIoTServerDB::clearValues(){
    _values.clear();
}



bool pIoTServerDB::insertValues(map<string,string>  values, time_t when){

    bool didUpdate = false;
    if(when == 0)
        when = time(NULL);

    for (auto& [key, value] : values) {
        if(insertValue(key, value, when, _eTag )){
            didUpdate = true;
        }
    }

    if(didUpdate)
        nextEtag();

    return didUpdate;

};

bool pIoTServerDB::insertValue(string key, string value) {
     bool didUpdate = false;

    if(insertValue(key, value, 0, _eTag )){
        didUpdate = true;
    }

    if(didUpdate)
        nextEtag();

    return didUpdate;
}


bool pIoTServerDB::insertValue(string key, string value, time_t when, eTag_t eTag){

    bool updated = false;

    if(when == 0)
        when = time(NULL);

    valueSchema_t schema = schemaForKey(key);

    if((schema.tracking != TR_DONT_RECORD)
        && canMinMaxForUnit(unitsForKey(key))) {

         if(!_minMaxValues.count(key)){
            _minMaxValues[key] = MinMaxValue();
        }

        double dVal;
        if(stringToDouble(value, dVal)){

            time_t lastTime = _minMaxValues[key].lastTime();
            if(lastTime != TIME_MAX){

                int thisDate =  (int)(when / 86400);
                int lastDate = (int) (lastTime /86400) ;

                if(thisDate != lastDate){

                    double  minValue;
                    double  maxValue;

                    if(_minMaxValues[key].getMin(minValue)
                       && _minMaxValues[key].getMax(maxValue)){
                        insertRangeToDB(key, minValue,maxValue, when);
                     }
                }
            }

            _minMaxValues[key].setValue(when, dVal);
         }

    }

     if(schema.tracking == TR_IGNORE){
        return false;
    }
    updated = valueShouldUpdate(key,value);

    _values[key] = make_pair(when, value);

    if(updated){
        if(schema.tracking == TR_DONT_RECORD) {
        }
        else if(schema.tracking == TR_TRACK_LATEST_VALUE)
        {
            saveUniqueValueToDB(key, value, when);
        }
        else if(schema.tracking == TR_TRACK_CHANGES)
        {
            insertValueToDB(key, value, when);
        }
        else if(schema.tracking == TR_TRACK_RANGE){
        };
        _etagMap[key] = eTag;
    }

    return updated;
}


vector<string> pIoTServerDB::keysChangedSinceEtag( eTag_t tag){
    vector<string> changeList;
    changeList.clear();

    for (auto& [key, t] : _etagMap) {

        if(tag <= t){
            changeList.push_back(key);
        }
    }

    return changeList;
}

bool pIoTServerDB::valueShouldUpdate(string key, string value){

    bool shouldInsert = true;
    double triggerDiff = 0;

    valueSchema_t schema = schemaForKey(key);
    if(schema.tracking == TR_IGNORE)
        return false;

    if(_values.count(key)){
        auto lastpair = _values[key];
        valueSchema_t vs = _schema[key];

        if(vs.units == IGNORE)
            return false;

        if(lastpair.second == value)
            return false;

        char *p1, *p;
        double newVal = strtod(value.c_str(), &p);
        double oldval = strtod(lastpair.second.c_str(), &p1);
        if(*p == 0 && *p1 == 0 ){

            double diff = abs(oldval-newVal);

            switch (vs.units) {
                case DEGREES_C:
                    triggerDiff = 5.0/9.0;
                    break;

                case RH:
                    triggerDiff = 1.0;
                    break;

                case HPA:
                    triggerDiff =  3.386389;
                    break;

                case MILLIVOLTS:
                case MILLIAMPS:
                case MAH:
                    triggerDiff = 500;
                    break;

                case WATTS:
                    triggerDiff = 5;
                    break;

                case VOLTS:
                    triggerDiff = 0.5;
                    break;

                case AMPS:
                    triggerDiff = 1.0;
                    break;

                case PERCENT:
                    triggerDiff = 1;
                    break;

                case HERTZ:
                    triggerDiff = 10;
                    break;

                default:
                    triggerDiff = 0;
                    break;
            }

            shouldInsert = diff > triggerDiff;
        }
    }

    return shouldInsert;
}


pIoTServerDB::valueSchema_t pIoTServerDB::schemaForKey(string key){

    valueSchema_t schema = {};
    schema.title = "";
    schema.units = UNKNOWN;
    schema.tracking = TR_IGNORE;
    schema.readOnly = true;

    if(_schema.count(key)){
        schema =  _schema[key];
    }

    return schema;
}

bool pIoTServerDB::isValidDataTypeForKey(string key, string value){
    bool isValid = false;

    string val = Utils::trim(value);
    bool boolState = false;

    valueSchemaUnits_t units = unitsForKey(key);
    if(units == UNKNOWN)  isValid = false;
    else if(units == STRING) isValid = true;
    else if(units == BINARY && (isBinaryString(val) || isHexString(val))) isValid = true;
    else if(units == BOOL)  isValid = stringToBool(val,boolState);
    else if(units == ACTUATOR) isValid = true;
    else  if (isNumberString(value)) isValid = true;
    else if( stringToBool(value,boolState)) isValid = true;

    return isValid;
}


void pIoTServerDB::addSchema(string key,
                             valueSchemaUnits_t units,
                             string title,
                             valueTracking_t tracking,
                             bool            readOnly
                             ){

    valueSchema_t sc = {};
    sc.units = units;
    sc.title = title;
    sc.tracking = tracking;
    sc.readOnly = readOnly;

    _schema[key] = sc;
}

json pIoTServerDB::schemaJSON(string key){
    json schemaList;

    if(_schema.count(key)){
        json entry;
        auto sch = _schema[key];

        entry[string(PROP_TITLE)]       =   sch.title;
        entry[string(JSON_ARG_UNITS)]   =   sch.units;
        entry[string(PROP_TRACKING)]    =  stringForTrackingValue(sch.tracking);
        entry[string(JSON_ARG_SUFFIX)]  =  unitSuffixForKey(key);
        schemaList[key] = entry;

    }
    return schemaList;
}

json pIoTServerDB::schemaJSON(){

    json schemaList;

    for (auto& [key, sch] : _schema) {
        json entry;
        entry[string(PROP_TITLE)]       =   sch.title;
        entry[string(JSON_ARG_UNITS)]   =   sch.units;
        entry[string(PROP_TRACKING)]    =   stringForTrackingValue(sch.tracking);
        entry[string(JSON_ARG_SUFFIX)]  =  unitSuffixForKey(key);
        schemaList[key] = entry;
    }

    return schemaList;
}


valueSchemaUnits_t pIoTServerDB::unitsForKey(string key){
    valueSchema_t schema = schemaForKey(key);
    return schema.units;
}

bool pIoTServerDB::canMinMaxForUnit(valueSchemaUnits_t unit){
    bool canMinMax = false;

    switch(unit){
        case MAH:
        case INT:
        case PERCENT:
        case WATTS:
        case MILLIVOLTS:
        case MILLIAMPS:
        case DEGREES_C:
        case VOLTS:
        case AMPS:
        case RH:
        case HPA:
        case FLOAT:
        case LUX:
            canMinMax = true;
             break;

        default:  break;
    }
    return canMinMax;
 }

bool pIoTServerDB::isUnitNumeric(valueSchemaUnits_t unit){
    bool isNumber = false;

    switch(unit){
        case STRING:
        case INVALID:
        case IGNORE:
        case UNKNOWN:
            isNumber = false;
            break;
        default:
            isNumber = true;
    }

    return isNumber;
}

string  pIoTServerDB::normalizeStringForUnit(string val,  valueSchemaUnits_t unit){

    string outStr = val;

    try {
        if(pIoTServerDB::isUnitNumeric(unit)){

            if(unit == INT){
                int num = stoi(val.c_str());
                outStr = to_string(num);
            } else if(unit == BOOL){
                int num = stoi(val.c_str());
                if(num == 0)
                    outStr = "0";
                else
                    outStr = "1";
            }
            else if(unit == BINARY){
                if(isBinaryString(val)){
                    std::bitset<8> bits(val);
                    outStr = bits.to_string();
                }
                else {
                    int num = stoi(val.c_str());
                    std::bitset<8> bits(num);
                    outStr = bits.to_string();
                }
            }
            else if(val.size() == 0){
                outStr = "0.0";
            }
        }

     } catch (const std::invalid_argument& ia) {
         std::cerr << "Caught an invalid argument exception: " << ia.what() << std::endl;
         std::cerr <<"normalizeStringForUnit("+val +")" << std::endl;
    }

    return outStr;
}


string   pIoTServerDB::unitSuffixForKey(string key){
    string suffix = {};

    switch(unitsForKey(key)){

        case MILLIVOLTS:
        case VOLTS:
            suffix = "V";
            break;

        case MILLIAMPS:
        case AMPS:
            suffix = "A";
            break;

        case MAH:
            suffix = "Ahrs";
            break;

        case DEGREES_C:
            suffix = "ºC";
            break;

        case RH:
        case PERCENT:
            suffix = "%";
            break;

        case WATTS:
            suffix = "W";
            break;

        case SECONDS:
            suffix = "Seconds";
            break;

        case MINUTES:
            suffix = "Minutes";
            break;

        case HERTZ:
            suffix = "Hz";
            break;

        case HPA:
            suffix = "inHg";
            break;

        default:
            break;
    }

    return suffix;
}



double pIoTServerDB::normalizedDoubleForValue(string key, string value){

    double retVal = 0;

    char   *p;
    double val = strtod(value.c_str(), &p);
    if(*p == 0) {

        switch(unitsForKey(key)){

            case MILLIVOLTS:
            case MILLIAMPS:
            case MAH:
                retVal = val / 1000;
                break;
            case PERCENT:
            case DEGREES_C:
            case WATTS:
            case VOLTS:
            case AMPS:
            case SECONDS:
            case MINUTES:
            case HPA:
            case HERTZ:
            case LUX:
            case POM:
            case RH:
                retVal = val;
                break;

            default:
                break;
        }
    }
    return retVal;
}
int pIoTServerDB::intForValue(string key, string value){

    int retVal = 0;

    switch(unitsForKey(key)){
        case MINUTES:
        case SECONDS:
        case INT:
        case BOOSTER:
        case MASTER_RELAY:
        case BOOL:
        {
            int intval = 0;

            if(sscanf(value.c_str(), "%d", &intval) == 1){
                retVal = intval;
            }
        }
            break;


        default:
            break;
    }

    return retVal;
}


string pIoTServerDB::displayStringForValue(string key, string value){

    string  retVal = value;
    string suffix =  unitSuffixForKey(key);

    switch(unitsForKey(key)){
        case POM: {
            double phase = normalizedDoubleForValue(key, value);
            retVal =  Lunar::GetSegmentName(phase);
         }
            break;
        case BOOL:
        case BOOSTER:
        case MASTER_RELAY:
        {
            int val  = intForValue(key, value);
            retVal = val?"true":"false";
        }
            break;

        case HPA:
        {
            double val = normalizedDoubleForValue(key, value);
            double pressure =  val * 0.00029530;

            char buffer[12];
            snprintf(buffer, sizeof(buffer), "%3.2f %s", pressure, suffix.c_str());
            retVal = string(buffer);
        }
            break;

        case ACTUATOR:
        {
            int intval = 0;

            if(sscanf(value.c_str(), "%d", &intval) == 1){
                retVal = Actuator_Device::displayStringForState(
                                                                static_cast<Actuator_Device::in_state_t>(intval));
            }
        }
               break;

        case RH:
        case MILLIVOLTS:
        case MILLIAMPS:
        case MAH:
        case VOLTS:
        case AMPS:
        case HERTZ:
        case WATTS:
        case LUX:
        case PERCENT:
        {
            double val = normalizedDoubleForValue(key, value);
            char buffer[12];
            snprintf(buffer, sizeof(buffer),  "%3.2f%s", val, suffix.c_str());
            retVal = string(buffer);
        }
            break;

        case DEGREES_C:
        {
            double val = normalizedDoubleForValue(key, value);
            double tempF =  val * 9.0 / 5.0 + 32.0;

            char buffer[12];
            snprintf(buffer, sizeof(buffer), "%3.2f%s", tempF, "°F");
            retVal = string(buffer);
        }
            break;
        case TIME_T:
        {
            time_t timeVal;
            if(sscanf(value.c_str(), "%ld", &timeVal) == 1){
                retVal = TimeStamp(timeVal, true).RFC1123String();
            }
        }
            break;
        case SECONDS:
        case MINUTES:
        {
            int val  = intForValue(key, value);
            if(val == -1){
                retVal = "Infinite";
            }
            else
            {
                char buffer[64];
                snprintf(buffer, sizeof(buffer),  "%d %s", val, suffix.c_str());
                retVal = string(buffer);
            }
        }
            break;

        default:
            break;
    }

    return retVal;
}


void pIoTServerDB::dumpMap(){

    timestamp::TimeStamp ts;

    printf("\n -- %s --\n", ts.logFileString().c_str());

    for (auto& [key, value] : _values) {

        auto lastpair = _values[key];
        auto count = _values.count(key);

        string desc = "";
        if(_schema.count(key)){
            desc = _schema[key].title;
        }

        printf("%3d %-8s:%10s %s\n",
               (int)count,
               key.c_str(),
               lastpair.second.c_str(),
               desc.c_str()
               );
    }
}

bool pIoTServerDB::isKeyInDB(string key){
    return _values.count(key);
}


json pIoTServerDB::currentJSONForKeys(stringvector keys){

    std::lock_guard<std::mutex> lock(_mutex);

    json j;

    for (auto key  : keys) {

        if(_values.count(key)){
            auto lastpair = _values[key];
            time_t t = lastpair.first;

            json entry;
            entry[string(JSON_ARG_VALUE)]        =   jsonForValue(key, lastpair.second);
            entry[string(JSON_ARG_DISPLAYSTR)]   =   displayStringForValue(key, lastpair.second);
            entry[string(JSON_ARG_TIME)]         =   t;

            if(_schema.count(key)){
                entry[string(PROP_TITLE)]        = _schema[key].title;
            }

            if(isKeyInManualMode(key))
                entry[string(JSON_VAL_AUTO)]   = false;


            j[key] = entry;
        }
    }

    if(j.empty()){
        j = json::object();
    }
     return j;
}


json pIoTServerDB::currentJSONForKey(string key){
    json j;

    if(_values.count(key)){
        auto lastpair = _values[key];

        time_t t = lastpair.first;

        json entry;
        entry[string(JSON_ARG_VALUE)]       =   jsonForValue(key, lastpair.second);
        entry[string(JSON_ARG_DISPLAYSTR)]  =   displayStringForValue(key, lastpair.second);
        entry[string(JSON_ARG_TIME)]         =   t;
        j[key] = entry;
    }

    return j;
}


json pIoTServerDB::jsonForValue(string key, string value){
    json j;

    switch(unitsForKey(key)){

        case MILLIVOLTS:
        case MILLIAMPS:
        case MAH:
        case VOLTS:
        case AMPS:
        case HERTZ:
        case WATTS:
        case HPA:
        case POM:
        case DEGREES_C:
        case LUX:
        {
            j = value;
            double val = normalizedDoubleForValue(key, value);
            j = to_string(val);
        }
            break;

        case SECONDS:
        case MINUTES:
        {
            int val  = intForValue(key, value);
            j = to_string(val);
        }
            break;

        case BOOL:
        {
            bool state = false;
            stringToBool(value,state);
            j =  to_string(state);
            break;
        }

         default:
            j = value;
            break;
    }
    return j;
}

json pIoTServerDB::currentValuesJSON(eTag_t  eTag){
    std::lock_guard<std::mutex> lock(_mutex);

    json j;

    for (auto& [key, value] : _values) {

        if(eTag != 0){
            auto k = key;
            vector<string> v =  keysChangedSinceEtag(eTag);

            bool found = std::any_of(v.begin(), v.end(),
                                     [k](std::string const& s) {return s==k;});
            if(!found) continue;
        }

        auto lastpair = _values[key];

        time_t t = lastpair.first;

        json entry;
        entry[string(JSON_ARG_VALUE)]       =   jsonForValue(key, lastpair.second);
        entry[string(JSON_ARG_DISPLAYSTR)]  =   displayStringForValue(key, lastpair.second);
        entry[string(JSON_ARG_TIME)]        =   t;

        if(_schema.count(key)){
            entry[string(PROP_TITLE)]        = _schema[key].title;
        }

        if(isKeyInManualMode(key))
            entry[string(JSON_VAL_AUTO)]   = false;

        j[key] = entry;
    }

    if(j.empty()){
        j = json::object();
    }
     return j;
}


bool pIoTServerDB::setKeyManualMode(string key, bool manual){

    if(unitsForKey(key) == valueSchemaUnits_t::BOOL){
        if(manual)
            _keysInManualMode.insert(key);
        else
            _keysInManualMode.erase(key);

        saveProperties();
        return true;
    }
    return false;
}

bool pIoTServerDB::isKeyInManualMode(string key){
    return _keysInManualMode.count(key)?true:false;
}

stringvector pIoTServerDB::keysInManualMode(){
    stringvector keys;
     for(string key : _keysInManualMode) keys.push_back(key);
    return keys;
}


// MARK: - Value Snapshot for expression evaluator

bool pIoTServerDB::snapshotForKV(string key, string val, numericValueSnapshot_t *snapshot){

    bool isValid = false;

    valueSchema_t vs = _schema[key];
    valueSchemaUnits_t units = vs.units;

    if(isUnitNumeric(units)){
        double dValue = 0;

        if(units == BOOL) {
            bool boolState;
            if(stringToBool(val,boolState)){
                dValue = boolState?1:0;
                isValid = true;
            }
        }
        else if(units == BINARY){
            std::bitset<8> bits(val);
            dValue = bits.to_ullong();
            isValid = true;
        }
        else if (isNumberString(val)){
            char   *p;
            double number = strtod(val.c_str(), &p);
            if(*p == 0){
                dValue = number;
                isValid = true;
            }
        }
        else if(val.size() == 0){
            dValue = 0;
            isValid = true;
        }

        if(isValid && snapshot != NULL){

            snapshot->readOnly  =  vs.readOnly;
            snapshot->value = dValue;
            snapshot->mirrorValue = dValue;
            snapshot->name  = key;
            snapshot->wasUpdated = false;
        }
    }

    return isValid;
}


bool pIoTServerDB::createValueSnapshot(vector<numericValueSnapshot_t> * snapshot,
                                       keyValueMap_t kv){
    std::lock_guard<std::mutex> lock(_mutex);


    vector<numericValueSnapshot_t> entries = {};

    for (auto& [key, e] : _values) {

        numericValueSnapshot_t s = {};
        string val = e.second;

        if(snapshotForKV(key, val, &s)){
            entries.push_back(s);
        }
    }

    for (auto& [key, val] : kv) {
        numericValueSnapshot_t s = {};

        if(snapshotForKV(key, val, &s)){
            entries.push_back(s);
        }
    }

    if(entries.size()){
        if(snapshot)
            *snapshot = entries;
        return true;
    }

    return false;
}
