//
//  pIoTServerDB_Core.cpp
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


//  pIoTServerDB.cpp
//
//  Created by Vincent Moscaritolo on 5/8/22.
//

#include "pIoTServerDB.hpp"
#include "PropValKeys.hpp"


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
#include <filesystem> // C++17

//
//#if defined(__APPLE__)
//#include <format>
//#else
//#include <fmt/format.h>
//#endif


#include <fstream>
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

string stringforSchemaUnits(valueSchemaUnits_t unit){
    string result;

    switch(unit){
        case INVALID:
            result = "INVALID";
            break;

        case UNKNOWN:
            result = "UNKNOWN";
            break;

        case BOOL:                // Bool ON/OFF
            result = "BOOL";
            break;

        case INT:                // Int
            result = "INT";
            break;

        case MAH:                // mAh milliAmp hours
            result = "MAH";
            break;

        case PERCENT:         // (per hundred) sign ‰
            result = "PERCENT";
            break;

        case WATTS:             // W
            result = "WATTS";
            break;

        case MILLIVOLTS:        // mV
            result = "MILLIVOLT";
            break;

        case MILLIAMPS:        // mA
            result = "MILLIAMPS";
            break;

        case SECONDS:            // sec
            result = "SECONDS";
            break;

        case MINUTES:            // mins
            result = "MINUTES";
            break;

        case DEGREES_C:        // degC
            result = "DEGREES_C";
            break;

        case VOLTS:            // V
            result = "VOLTS";
            break;

        case HERTZ:            // Hz
            result = "HERTZ";
            break;

        case AMPS:                // A
            result = "AMPS";
            break;

        case BINARY:            // Binary 8 bits 000001
            result = "BINARY";
            break;

        case RH:                // Relative Humidity Percentage
            result = "RH";
            break;

        case HPA:               // barometric pressure in Hectopascal * 0.00029530 to get inHg
            result = "HPA";
            break;

        case STRING:            // string
            result = "STRING";
            break;

        case IGNORE:
            result = "IGNORE";
            break;

        case TIME_T:             // unix time
            result = "TIME_T";
            break;

        case FLOAT:              // floating number
            result = "FLOAT";
            break;

        case POM:                // phase of moon  0 - 1: 0.5 = full:
            result = "POM";
            break;

        case EQUATION:           // equation string
            result = "EQUATION";
            break;

        case LUX:                // Lux is used to measure the amount of light output in a given area.
            // One lux is equal to one lumen per square meter.
            result = "LUX";
            break;

        case ACTUATOR:           //  ACTUATOR action
            result = "ACTUATOR";
            break;

        case BOOSTER:           //  ACTUATOR action
            result = "BOOSTER";
            break;

        case MASTER_RELAY:           //  ACTUATOR action
            result = "MASTER_RELAY";
            break;

        case SERIAL_NO:          //Serial Number (String)
            result = "SERIAL_NO";
            break;
    }

    return result;
}

valueSchemaUnits_t schemaUnitsForString(string str){
    valueSchemaUnits_t units = INVALID;

    static  map<string,valueSchemaUnits_t> scMap = {
        {"BOOL" , BOOL},
        {"INT" , INT},
        {"STRING", STRING},
        {"BINARY", BINARY},
        {"FLOAT", FLOAT},
        {"SECONDS", SECONDS},
        {"MINUTES", MINUTES},
        {"mA",MILLIAMPS},            // mA
        {"mV", MILLIVOLTS},            // mV
        {"VOLTS" , VOLTS},
        {"AMPS" , AMPS},
        {"TEMPERATURE" , DEGREES_C},
        {"HUMIDITY" , RH},
        {"BAROMETRIC" , HPA},
        {"EQUATION" , EQUATION},
        {"PERCENT" , PERCENT},
        {"LUX" , LUX},
        {"ACTUATOR" , ACTUATOR},
        {"BOOSTER" , BOOSTER},
        {"MASTER_RELAY" , MASTER_RELAY},
        {"SERIAL_NO" , SERIAL_NO},

    };


    auto it =  scMap.find(str);
    if (it != scMap.end()) units = it->second;

    return units;
}

valueTracking_t trackingValueForString(string str){
    valueTracking_t tr = TR_IGNORE;

    if(caseInSensStringCompare(str, string(JSON_ARG_DONT_RECORD))) {
        tr = TR_DONT_RECORD;
    } else  if(caseInSensStringCompare(str, string(JSON_ARG_TRACK_LATEST_VALUE))) {
        tr = TR_TRACK_LATEST_VALUE;
    } else  if(caseInSensStringCompare(str, string(JSON_ARG_TRACK_CHANGES))) {
        tr = TR_TRACK_CHANGES;
    } else  if(caseInSensStringCompare(str, string(JSON_ARG_TRACK_RANGE))) {
        tr = TR_TRACK_RANGE;
}

    return tr;
}

string stringForTrackingValue(valueTracking_t tr){
    string str =  string(JSON_ARG_IGNORE);

    switch (tr) {
        case TR_DONT_RECORD:
            str = string(JSON_ARG_DONT_RECORD);
            break;

        case TR_TRACK_LATEST_VALUE:
            str = string(JSON_ARG_TRACK_LATEST_VALUE);
            break;

        case TR_TRACK_CHANGES:
            str = string(JSON_ARG_TRACK_CHANGES);
            break;

        case TR_TRACK_RANGE:
            str = string(JSON_ARG_TRACK_RANGE);
            break;

        default:
              break;
    }
    return  str;
}
// MARK: - MinMaxValue

#define DBL_MAX std::numeric_limits<double>::max()
#define TIME_MAX    std::numeric_limits<time_t>::max()

MinMaxValue::MinMaxValue(){
    commonInit();
};


void MinMaxValue::commonInit(){

    _lastTime = TIME_MAX;

    for(int i = 0; i < 24; i++){
        _entries[i].minValue = DBL_MAX;
        _entries[i].maxValue =  DBL_MAX;
    }
}


void MinMaxValue::setValue(time_t when, double value){

    int hour = (when / 3600) % 24;
 //   cout << "when " << hour <<endl;

    if((_lastTime != TIME_MAX) && (when - _lastTime > 3600)){
        int start = (int) _lastTime/3600;
        int end = (int) when/3600;

 //       cout <<  "start: " << start << " " << end <<endl;

        for(int i = start; i < end; i++){
            int offset = i %24;

            _entries[offset].maxValue = DBL_MAX;
            _entries[offset].minValue = DBL_MAX;

//            cout <<  offset <<endl;
          }
     };



    if( _entries[hour].maxValue == DBL_MAX){
        _entries[hour].maxValue = value;
        _entries[hour].minValue = value;
    }
    else
    {
        if(value > _entries[hour].maxValue)
            _entries[hour].maxValue =  value;
        else if(value < _entries[hour].minValue)
            _entries[hour].minValue =  value;
    }

    _lastTime = when;
};

bool MinMaxValue::getMax(double &value){

    double val = DBL_MAX;

    for(int i = 0; i < 24; i++){
        if(_entries[i].maxValue != DBL_MAX){
            double d = _entries[i].maxValue;
            if(val == DBL_MAX)
                val = d;
            else if( d > val)
                val = d;
        }
    }

    if(val != DBL_MAX){
        value = val;
        return true;
    }
    return false;
}

bool MinMaxValue::getMin(double &value){
    double val = DBL_MAX;

    for(int i = 0; i < 24; i++){
        if(_entries[i].maxValue != DBL_MAX){
            double d = _entries[i].minValue;

            if(val == DBL_MAX)
                val = d;
            else if( d < val)
                val = d;
        }
    }

    if(val != DBL_MAX){
        value = val;
        return true;
    }
    return false;
}

// MARK: - pIoTServerDB

pIoTServerDB::pIoTServerDB (){
    _eTag = 1;
    _etagMap.clear();

    _values.clear();
    _props.clear();
    _keysInManualMode.clear();

    _sdb = NULL;

    // create RNG engine
    constexpr std::size_t SEED_LENGTH = 8;
    std::array<uint_fast32_t, SEED_LENGTH> random_data;
    std::random_device random_source;
    std::generate(random_data.begin(), random_data.end(), std::ref(random_source));
    std::seed_seq seed_seq(random_data.begin(), random_data.end());
    _rng =  std::mt19937{ seed_seq };

    _schemaMap = {
        {"Bool", BOOL},                // Bool ON/OFF
        {"Int", INT},                // Int
        {"Float",   FLOAT},                    // Floating point
        {"mAh", MAH},                // mAh milliAmp hours
        {"%", PERCENT} ,            // (per hundred) sign PERCENT
        {"W", WATTS},                 // W
        {"mA",MILLIAMPS},            // mA
        {"mV", MILLIVOLTS},            // mV
        {"sec", SECONDS},            // sec
        {"mins",MINUTES},            // mins
        {"degC", DEGREES_C},        // degC
        {"V", VOLTS},                // V
        {"Hz", HERTZ},                // Hz
        {"A", AMPS},                    // A
        {"Binary", BINARY},            // Binary 8 bits 000001
        {"String", STRING},                // string
        {"HPA",   HPA},                    // Hectopascals
        {"Unix Time",   TIME_T},        // Unix Time
        {"Equation", EQUATION},         // equation
        {"ignore", IGNORE},                // ignore
        {"Actuator", ACTUATOR},      // ACTUATOR position code
        {"SERIAL_NO" , SERIAL_NO}      // SERIAL Number

    };
    _didChangeProperties  = false;

    setupDatabasePeriodicTasks();
}


pIoTServerDB::~pIoTServerDB (){
    if(_sdb)
    {
        sqlite3_close(_sdb);
        _sdb = NULL;
    }
}

void pIoTServerDB::setupDatabasePeriodicTasks(){

    sequenceID_t sid;

    Sequence seq = Sequence(
                            EventTrigger(string("{\"cron\": \"@daily\"}")),
                            Action([=, this](EventTrigger){
                                runDailyTask();
                                return true;
                            })
                            );
    seq.setName("Database Daily Tasks: cron");
    seq.setSouldIgnoreLog(true);
    seq.setEnable(true);
    sequenceSave(seq,&sid);
}

void pIoTServerDB::runDailyTask(){

    // no longer needed we do that automatically in insertValue()
}

bool pIoTServerDB::refreshSolarEvents(){

    string str;
    bool success = false;

    success = getConfigProperty(string(PROP_CONFIG_LATLONG), str);
    if(success && !str.empty()){

        double latitude, longitude;
        int n;

        if( sscanf(str.c_str(), "%lf,%lf%n", &latitude, &longitude, &n) == 2) {
            SolarTimeMgr::shared()->setLatLong(latitude ,longitude);
            return SolarTimeMgr::shared()->calculateSolarEventTimes();
        }
    }
    return false;
}





// MARK: -   SERVER PORTS

void  pIoTServerDB::setRESTPort(int){
}

int pIoTServerDB::getRESTPort(){
    return 8081;
}




// MARK: - Utility

string pIoTServerDB::makeNonce(){

    u_long num;
    string nonce;

    std::uniform_int_distribution<long> distribution(0,999999);
    num = distribution(_rng);

    std::stringstream ss;
    ss << std::setw(6) << std::setfill('0') << num;

    nonce  = ss.str();

    return nonce;
}
