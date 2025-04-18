//
//  RadDB.cpp
//  carradio
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
        
        {"VOLTS" , VOLTS},
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
                            Action([=, this](EventTrigger trig){
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
//
//    vector<minMaxEntry_t> minMaxResults;
//    {
//        std::lock_guard<std::mutex> lock(_mutex);
//
//        // save daily min max range
//        for(auto &[key,mm]: _minMaxValues){
//            valueSchema_t schema = schemaForKey(key);
//            
//            if(schema.tracking == TR_TRACK_RANGE){
//                minMaxEntry_t entry = {
//                    .key = key,
//                    .maxValue = DBL_MAX,
//                    .minValue = DBL_MAX
//                };
//          
//                if(mm.getMin(entry.minValue)
//                   && mm.getMax(entry.maxValue)){
//                    minMaxResults.push_back(entry) ;
//                }
//            }
//        }
//    }
//    
//    if(minMaxResults.size()){
//        time_t now = time(NULL);
//        for(auto entry:minMaxResults){
//            insertRangeToDB(entry.key, entry.minValue,entry.maxValue,now);
//        }
//    }
    
    
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
            
       /*
        if the range date changed, then we should record the min/max in the database
        */
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
    // check if this is going to cause a change
    updated = valueShouldUpdate(key,value);
    
    // update value
    _values[key] = make_pair(when, value);
    
    if(updated){
        if(schema.tracking == TR_DONT_RECORD) {
            //  keep last value
            //  but dont put this in the database
        }
        else if(schema.tracking == TR_TRACK_LATEST_VALUE)
        {
            // only keep last value
            //  Add these to DB - but dont insert.. just update.
            saveUniqueValueToDB(key, value, when);
        }
        else if(schema.tracking == TR_TRACK_CHANGES)
        {
            // record in DB
            insertValueToDB(key, value, when);
        }
        else if(schema.tracking == TR_TRACK_RANGE){
            //  keep last value
            //  but dont put this in the database
            // we update this data later
 
        };
        // we still need to bump the ETAG on changes
        _etagMap[key] = eTag;
    }
    
    //        printf("%s %s: %s \n", updated?"T":"F",  key.c_str(), value.c_str());
    
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
        
        // do we ignore it
        if(vs.units == IGNORE)
            return false;
        
        // quick string compare to see if its the same
        if(lastpair.second == value)
            return false;
        
        // see if it's a number
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
                    // .1 inches of mercury = 3.386389 HPA
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
            
//            if(vs.units == DEGREES_C)
//                      printf("%s %8s %5.3f -  %5.3f = %5.3f > %5.3f\n", shouldInsert?"T":"F", key.c_str(),
//                          oldval, newVal, diff , triggerDiff );
        }
    }
    
    return shouldInsert;
}


pIoTServerDB::valueSchema_t pIoTServerDB::schemaForKey(string key){
    
    valueSchema_t schema = {"",UNKNOWN,TR_IGNORE};
    
    // check for specific schema
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
    else if(units == ACTUATOR) isValid = true;  // will take anything
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
    
    valueSchema_t sc;
    sc.units = units;
    sc.title = title;
    sc.tracking = tracking;
    
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
        case MILLIVOLTS:       // mV
        case MILLIAMPS:       // mA
        case DEGREES_C:       // degC
        case VOLTS:           // V
        case AMPS:             // A
        case RH:               // Relative Humidity Percentage
        case HPA:              // barometric pressure in Hectopascal * 0.00029530 to get inHg
        case FLOAT:             // floating number
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
    
    // see if it's a number
    char   *p;
    double val = strtod(value.c_str(), &p);
    if(*p == 0) {
        
        // normalize number
        
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
        case BOOSTER:               //  BOOSTER Relay POSITION
        case MASTER_RELAY:           //  MASTER RELAY POSITION
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
        case BOOSTER:               //  BOOSTER Relay POSITION 
        case MASTER_RELAY:           //  MASTER RELAY POSITION
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
    
    //    string suffix =  unitSuffixForKey(key);
    
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
            // force a 1 or 0 always
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
//  #warning write code for hex values?
        //     else if(units == BINARY && (isBinaryString(val) || isHexString(val))) isValid = true;
        
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
 

// MARK: - properties
bool pIoTServerDB::setProperty(string key, string value){
    
    bool shouldUpdate =
    (_props.count(key) == 0)
    ||(_props[key] != value) ;
    
// we used to allow changing the lat long from the REST
// but no longer, this is a config var,  change it in the prop file
//    if(key ==  PROP_CONFIG_LATLONG){
//        refreshSolarEvents();
//    }
    
    if(shouldUpdate) {
        _props[key] = value;
        _didChangeProperties  = shouldUpdate;
        saveProperties();
        
    }
    
    return true;
}

bool pIoTServerDB::removeProperty(string key){
    
    if(_props.count(key)){
        _props.erase(key);
        saveProperties();
        return true;
    }
    return false;
}



bool pIoTServerDB::setPropertyIfNone(string key, string value){
    
    if(_props.count(key) == 0){
        _props[key] = value;
        saveProperties();
        return true;
    }
    return false;
}


vector<string> pIoTServerDB::propertiesKeys(){
    
    vector<string> keys = {};
    for(auto it =  _props.begin(); it != _props.end(); ++it) {
        keys.push_back(it.key());
    }
    return keys;
}


bool pIoTServerDB::getAllProperties(vector<string_view> filter, nlohmann::json  *j){
    
    stringvector allKeys = propertiesKeys();
    
    nlohmann::json entries;
    if(filter.size())
    {
        json items;
        for(auto it =  _props.begin(); it != _props.end(); ++it) {
            
            auto found = std::find(filter.begin(), filter.end(), it.key());
            if (found == filter.end()){
                items[it.key()] = it.value();
            }
            *j = items;
        }
    }
    else
    {
        *j = _props;
    }
    
    return  true;
}



bool pIoTServerDB::setProperty(string key, nlohmann::json  value){
    
    bool shouldUpdate =
    (_props.count(key) == 0)
    ||(_props[key] != value) ;
    
    if(shouldUpdate) {
        _props[key] = value;
        _didChangeProperties  = shouldUpdate;
    }
    
    return true;
}


bool pIoTServerDB::getProperty(string key, string *value){
    
    if( _props.contains(key)){
        
        if(_props.at(key).is_string()){
            if(value) *value  = _props.at(key);
        }else if(_props.at(key).is_number()){
            if(value) *value  =  to_string( _props.at(key));
        }
        return true;
    }
    return false;
}

bool pIoTServerDB::getTimeProperty(string key, time_t * valOut){
    if( _props.contains(key)
       &&  _props.at(key).is_number_unsigned())
    {
        auto val = _props.at(key);
        
        if(valOut)
            *valOut = (time_t) val;
        return true;
        
    }
    return false;
}



bool  pIoTServerDB::getIntProperty(string key, int * valOut){
    
    if( _props.contains(key)) {
        if(_props.at(key).is_number_integer()){
            auto val = _props.at(key);
            
            if(valOut)
                *valOut =  val;
            return true;
        }
        else     if(_props.at(key).is_string()){
            string val = _props.at(key);
            
            int intValue = atoi(val.c_str());
            if(valOut)
                *valOut =  intValue;
            return true;
            
        }
    }
    return false;
}


bool  pIoTServerDB::getUint16Property(string key, uint16_t * valOut){
    
    if( _props.contains(key)) {
        if(_props.at(key).is_number_unsigned()){
            auto val = _props.at(key);
            
            if(val <= UINT16_MAX){
                if(valOut)
                    *valOut = (uint16_t) val;
                return true;
            }
        }
        else     if(_props.at(key).is_string()){
            string val = _props.at(key);
            
            int intValue = atoi(val.c_str());
            if(intValue <= UINT16_MAX){
                if(valOut)
                    *valOut = (uint16_t) intValue;
                return true;
            }
        }
    }
    return false;
}

bool  pIoTServerDB::getFloatProperty(string key, float * valOut){
    
    if( _props.contains(key)
       &&  _props.at(key).is_number_float()) {
        auto val = _props.at(key);
        if(valOut)
            *valOut = (float) val;
        return true;
    }
    return false;
}

bool  pIoTServerDB::getBoolProperty(string key, bool * valOut){
    
    if( _props.contains(key) ){
        auto val = _props.at(key);
        
        if(_props.at(key).is_boolean()){
            if(valOut) *valOut = (bool)val;
            return true;
        }
        else     if(_props.at(key).is_string()){
            if(val == "true"){
                if(valOut) *valOut = true;
                return true;
            }
            else if(val == "false"){
                if(valOut)    *valOut = false;
                return true;
            }
        }
    }
    return false;
}




bool pIoTServerDB::getJSONProperty(string key, nlohmann::json  *valOut){
    
    if( _props.contains(key) ){
        auto val = _props.at(key);
        if(valOut)
            *valOut = val;
        return true;
    }
    
    return  false;
}

bool pIoTServerDB::getConfigProperty(string key, string &valOut){
   
    json config;
    if(getJSONProperty(string(PROP_CONFIG), &config)){
        if( config.contains(key) &&  config[key].is_string()){
                valOut = config[key];
                return true;
        }
    }
    return false;
}

bool pIoTServerDB::setConfigProperty(string key, string value){
    
    json config;
    
    if(!key.empty()
       &&   getJSONProperty(string(PROP_CONFIG), &config))
    {
        config[key] = value;
        _props.at(PROP_CONFIG) = config;
        
        _didChangeProperties  = true;;
        saveProperties();
        return true;
    }
 
    return false;
}

bool pIoTServerDB::removeConfigProperty(string key){
    
    json config;
    
    if(!key.empty()
       &&   getJSONProperty(string(PROP_CONFIG), &config))
    {
        if(config.count(key)){
            config.erase(key);
            _props.at(PROP_CONFIG) = config;
            
            _didChangeProperties  = true;;
            saveProperties();
            return true;
            
            
        }
    }
    return false;
}

 
 

//MARK: - Database Persistent operations

bool pIoTServerDB::restorePropertiesFromFile(string propFileNameIn, string assetDirPathIn){
    
    std::ifstream    ifs;
    bool                 statusOk = false;
     
    string  propFileName = propFileNameIn.empty()?defaultPropertyFileName():propFileNameIn;
    string  assetsPath = assetDirPathIn.empty()?"assets":assetDirPathIn;
    
    if(assetsPath.empty())
        _propertyFilePath = propFileName;
    else
        _propertyFilePath = assetsPath + "/" + propFileName;
  
    LOGT_DEBUG("OPEN Property File: %s", _propertyFilePath.c_str());
 
    try{
        string line;
        std::lock_guard<std::mutex> lock(_mutex);
        
        // open the file
        ifs.open(_propertyFilePath, ios::in);
        if(!ifs.is_open()) return false;
        
        _props.clear();
        ifs >> _props;
        
        statusOk = true;
        _didChangeProperties  = false;
        
        ifs.close();
         
        // load events
        for (json::iterator it = _props.begin(); it != _props.end(); ++it) {
            
            if( it.key() == PROP_SEQUENCE_GROUPS && it.value().is_array()){
                for (auto& el : it.value()) {
                    restoreSequenceGroupFromJSON(el);
                }
            }
            else if( it.key() == PROP_SEQUENCE && it.value().is_array()){
                for (auto& el : it.value()) {
                    if(el.is_object()){
                        Sequence seq = Sequence(el);
                        if(seq.isValid()&& ! sequenceIDIsValid(seq._rawSequenceID)){
                            _sequences[seq._rawSequenceID] = seq;
                        }
                    }
                }
            }
  
            else if( it.key() == JSON_ARG_MANUAL_KEYS && it.value().is_array()){
                _keysInManualMode.clear();
                for (auto& el : it.value()) {
                    _keysInManualMode.insert(el);
                }
             }
          }
  
        
        // remove EVENTS and PROP_EVENT_GROUPS from properties
    
        _props.erase(PROP_SEQUENCE);
        _props.erase(PROP_SEQUENCE_GROUPS);
  
        refreshSolarEvents();

    }
//    catch (json::parse_error& ex)
//    {
//        std::cerr << "parse error at byte " << ex.byte << std::endl;
//        statusOk = false;
//    }
    catch(std::ifstream::failure &err) {
        LOGT_ERROR("restorePropertiesFromFile:FAIL: %s", err.what());
        statusOk = false;
    }
    
    return statusOk;
}

bool pIoTServerDB::savePropertiesToFile(string propFileNameIn, string assetDirPathIn){
  
    string  propFileName = propFileNameIn.empty()?defaultPropertyFileName():propFileNameIn;
    string  assetsPath = assetDirPathIn.empty()?"assets":assetDirPathIn;
    
    if(assetsPath.empty())
        _propertyFilePath = propFileName;
    else
        _propertyFilePath = assetsPath + "/" + propFileName;
  
    LOGT_DEBUG("Save Property File: %s", _propertyFilePath.c_str());

    return saveProperties();
}


bool pIoTServerDB::saveProperties(){
    
    std::lock_guard<std::mutex> lock(_mutex);
    bool statusOk = false;
    
    time_t now = time(NULL);
    
    eTag_t fileEtag = 0;
  
 //   PROP_FILE_VERSION
    if(_props.count(PROP_FILE_VERSION)
       && _props[PROP_FILE_VERSION].is_object()){
        
        json j  = _props[PROP_FILE_VERSION];
        
        if(j.count(PROP_FILE_ETAG)
           && j[PROP_FILE_ETAG].is_number())
            fileEtag = j[PROP_FILE_ETAG];
      }
    
    json pfv;
    pfv[PROP_FILE_ETAG] = ++fileEtag;
    pfv[ PROP_LAST_WRITE_DATE] = now;
    _props[PROP_FILE_VERSION] = pfv;
    
    std::ofstream ofs;
      
    try{
        ofs.open(_propertyFilePath, std::ios_base::trunc);
        
        if(ofs.fail())
            return false;
        
        json jP = _props;
    
        if(_sequences.size() > 0){
            json j;
            for (const auto& [sid, _] : _sequences) {
                Sequence* seq =  &_sequences[sid];
                
                // dont save callback events
                if(seq->hasCallBackAction()) continue;
                
                j.push_back(seq->JSON());
            }
            
            jP[PROP_SEQUENCE] = j;
        }
 
        if(_sequenceGroups.size() > 0){
            json j;
            for (const auto& [seqGroupID, _] : _sequenceGroups) {
                json jEG;
                
                if(saveSequenceGroupToJSON(seqGroupID, jEG)){
                    j.push_back(jEG);
                }
            }
            jP[PROP_SEQUENCE_GROUPS] = j;
        }
        
        if(_keysInManualMode.size()){
            jP[JSON_ARG_MANUAL_KEYS] = _keysInManualMode;
          }
  
        string jsonStr = jP.dump(4);
        ofs << jsonStr << "\n";
        
        ofs.flush();
        ofs.close();
        
        statusOk = true;
        _didChangeProperties  = false;
        
    }
    catch(std::ofstream::failure &writeErr) {
        statusOk = false;
    }
    
    
    return statusOk;
}

string pIoTServerDB::defaultPropertyFileName(){
    return "piotserver.props.json";
}

// MARK: -  API Secrets
bool pIoTServerDB::apiSecretCreate(string APIkey, string APISecret){
    json config;

    if(APIkey.empty() || APISecret.empty())
        return false;
    
    if(getJSONProperty(string(PROP_CONFIG), &config)
       && config.contains(PROP_ACCESS_KEYS)
       &&  config[PROP_ACCESS_KEYS].is_object()){
        
        json accesskeys = config[PROP_ACCESS_KEYS];
        if(accesskeys.count(APIkey))
            return false;
        
        accesskeys[APIkey] = APISecret;
        
        config[PROP_ACCESS_KEYS] = accesskeys;
        _props.at(PROP_CONFIG) = config;
        
        _didChangeProperties  = true;;
        saveProperties();
        return true;
    }
    
    return false;
}

bool pIoTServerDB::apiSecretSetSecret(string APIkey, string APISecret){
    
    if(APIkey.empty() || APISecret.empty())
        return false;
    
    json config;
    
    if(!APIkey.empty()
       &&   getJSONProperty(string(PROP_CONFIG), &config)
       && config.contains(PROP_ACCESS_KEYS)
       &&  config[PROP_ACCESS_KEYS].is_object()){
        
        json accesskeys = config[PROP_ACCESS_KEYS];
        if(accesskeys.count(APIkey)){
            
            accesskeys[APIkey] = APISecret;
            
            config[PROP_ACCESS_KEYS] = accesskeys;
            _props.at(PROP_CONFIG) = config;
            
            _didChangeProperties  = true;;
            saveProperties();
            return true;
            
        }
    }
    
    return false;
}

bool pIoTServerDB::apiSecretDelete(string APIkey){
    
    json config;
    
    if(!APIkey.empty()
       &&   getJSONProperty(string(PROP_CONFIG), &config)
       && config.contains(PROP_ACCESS_KEYS)
       &&  config[PROP_ACCESS_KEYS].is_object()){
        
        json accesskeys = config[PROP_ACCESS_KEYS];
        if(accesskeys.count(APIkey)){
            accesskeys.erase(APIkey);
            
            config[PROP_ACCESS_KEYS] = accesskeys;
            _props.at(PROP_CONFIG) = config;
            
            _didChangeProperties  = true;;
            saveProperties();
            return true;
            
        }
    };
    
    return false;
}

bool pIoTServerDB::apiSecretGetSecret(string APIkey, string &APISecret){
    
     json config;
    if(getJSONProperty(string(PROP_CONFIG), &config)
     && config.contains(PROP_ACCESS_KEYS)
       &&  config[PROP_ACCESS_KEYS].is_object()){
        
        json accesskeys = config[PROP_ACCESS_KEYS];
  
        for (auto& ak : accesskeys.items()){
            if(ak.key() ==  APIkey) {
                if(ak.value().is_string()){
                    APISecret = ak.value();
                    return true;
                 }
                return false;
            }
        }
        return false;
    }
    // NO access_keys - secret always matches
    return false;
}

bool pIoTServerDB::apiSecretMustAuthenticate(){
    
    json config;
    if(getJSONProperty(string(PROP_CONFIG), &config)
       && config.contains(PROP_ACCESS_KEYS)
       &&  config[PROP_ACCESS_KEYS].is_object()
       && config[PROP_ACCESS_KEYS].size() > 0 )
        return true;
    
    return false;
}

// MARK: -   SERVER PORTS

void  pIoTServerDB::setRESTPort(int port){
}

int pIoTServerDB::getRESTPort(){
    return 8081;
}


// MARK: -  SQL DATABASE VALUES

bool pIoTServerDB::initLogDatabase(string assetPath){
    
    if(_sdb) {
        sqlite3_close(_sdb);
        _sdb = NULL;
    }
    
    string dbFileName = "piotserver.db";
    // create a file path
    
    if(assetPath.size())
        dbFileName = assetPath + "/" + dbFileName;
    
    LOGT_DEBUG("OPEN database: %s", dbFileName.c_str());
     
    //  Open database
    if(sqlite3_open(dbFileName.c_str(), &_sdb) != SQLITE_OK){
        LOGT_ERROR("sqlite3_open FAILED: %s", dbFileName.c_str(), sqlite3_errmsg(_sdb    ) );
        return false;
    }
    
    // make sure primary tables are there.
    string sql1 = "CREATE TABLE IF NOT EXISTS DEVICE_DATA("  \
    "NAME               TEXT         NOT NULL," \
    "DATE          NUMERIC    NOT NULL," \
    "VALUE         TEXT         NOT NULL);";
    
    char *zErrMsg = 0;
    if(sqlite3_exec(_sdb,sql1.c_str(),NULL, 0, &zErrMsg  ) != SQLITE_OK){
        LOGT_ERROR("sqlite3_exec FAILED: %s\n\t%s", sql1.c_str(), sqlite3_errmsg(_sdb    ) );
        sqlite3_free(zErrMsg);
        return false;
    }
    // make sure primary tables are there.
    string sql2 = "CREATE TABLE IF NOT EXISTS ALERT_LOG("  \
    "ALERT           INTEGER    NOT NULL," \
    "DATE          NUMERIC    NOT NULL, " \
    "DETAILS        TEXT );";
    
    if(sqlite3_exec(_sdb,sql2.c_str(),NULL, 0, &zErrMsg  ) != SQLITE_OK){
        LOGT_ERROR("sqlite3_exec FAILED: %s\n\t%s", sql2.c_str(), sqlite3_errmsg(_sdb    ) );
        sqlite3_free(zErrMsg);
        return false;
    }
    
    // make sure primary tables are there.
    string sql3 = "CREATE TABLE IF NOT EXISTS DEVICE_RANGE("  \
    "NAME               TEXT            NOT NULL," \
    "DATE               NUMERIC        NOT NULL," \
    "MIN                REAL            NOT NULL," \
    "MAX                REAL            NOT NULL" \
    ");";

    if(sqlite3_exec(_sdb,sql3.c_str(),NULL, 0, &zErrMsg  ) != SQLITE_OK){
        LOGT_ERROR("sqlite3_exec FAILED: %s\n\t%s", sql3.c_str(), sqlite3_errmsg(_sdb    ) );
        sqlite3_free(zErrMsg);
        return false;
    }
    
    if(!restoreValuesFromDB()){
        LOG_ERROR("restoreValuesFromDB FAILED\n");
        return false;
    }
    
    return true;
}

bool pIoTServerDB::restoreValuesFromDB(){
    
    std::lock_guard<std::mutex> lock(_mutex);
    bool    statusOk = true;;
    
    _values.clear();
    
    string sql = string("SELECT NAME, VALUE, MAX(DATE) FROM DEVICE_DATA GROUP BY NAME ORDER BY DATE DESC;");
   
    sqlite3_stmt* stmt = NULL;
    sqlite3_prepare_v2(_sdb, sql.c_str(), -1,  &stmt, NULL);
    
    while ( (sqlite3_step(stmt)) == SQLITE_ROW) {
        
        string  key = string( (char*) sqlite3_column_text(stmt, 0));
        string  value = string((char*) sqlite3_column_text(stmt, 1));
        time_t  when =  sqlite3_column_int64(stmt, 2);
        
   //     printf("%8s  %8s %ld\n",  key.c_str(),  value.c_str(), when );
        _values[key] = make_pair(when, value) ;
        _etagMap[key] = nextEtag();
    }
    sqlite3_finalize(stmt);
    
    
    return statusOk;
}



bool pIoTServerDB::insertValueToDB(string key, string value, time_t time ){
    
    std::lock_guard<std::mutex> lock(_mutex);
  
    string sql = string("INSERT INTO DEVICE_DATA (NAME,DATE,VALUE) ")
    + "VALUES  ('" + key + "', '" + to_string(time) + "', '" + value + "' );";
    
    // printf("%s\n", sql.c_str());
    
    char *zErrMsg = 0;
    if(sqlite3_exec(_sdb,sql.c_str(),NULL, 0, &zErrMsg  ) != SQLITE_OK){
        LOGT_ERROR("sqlite3_exec FAILED: %s\n\t%s", sql.c_str(), sqlite3_errmsg(_sdb    ) );
        sqlite3_free(zErrMsg);
        return false;
    }
    
    return true;
}


bool pIoTServerDB::insertRangeToDB(string key, double minVal, double maxVal, time_t time ){
  
    std::lock_guard<std::mutex> lock(_mutex);
  
    string sql = string("INSERT INTO DEVICE_RANGE (NAME,DATE,MIN,MAX) ")
    + "VALUES  ('" + key + "', '" + to_string(time)
    +  "', '" + to_string(minVal)
    +  "', '" + to_string(maxVal) + "' );";

 //   printf("%s\n", sql.c_str());
    
    char *zErrMsg = 0;
    if(sqlite3_exec(_sdb,sql.c_str(),NULL, 0, &zErrMsg  ) != SQLITE_OK){
        LOGT_ERROR("sqlite3_exec FAILED: %s\n\t%s", sql.c_str(), sqlite3_errmsg(_sdb    ) );
        sqlite3_free(zErrMsg);
        return false;
    }
    
    return true;
}



bool pIoTServerDB::saveUniqueValueToDB(string key, string value, time_t time ){
    
    std::lock_guard<std::mutex> lock(_mutex);
 
    string sql =
    string("BEGIN;")
    + string("DELETE FROM DEVICE_DATA WHERE NAME = '") + key + "';"
    + string("INSERT INTO DEVICE_DATA (NAME,DATE,VALUE) ")
    + "VALUES  ('" + key + "', '" + to_string(time) + "', '" + value + "' );"
    + string("COMMIT;");
    
    /*
     BEGIN;
     DELETE FROM DEVICE_DATA  WHERE NAME = 'TANK_RAW';
     INSERT INTO DEVICE_DATA (NAME,DATE,VALUE) VALUES  ('TANK_RAW', '2021-09-19 00:52:06 GMT', '1' );
     COMMIT;
     
     */
    //    printf("%s\n", sql.c_str());
    
    char *zErrMsg = 0;
    if(sqlite3_exec(_sdb,sql.c_str(),NULL, 0, &zErrMsg  ) != SQLITE_OK){
        LOGT_ERROR("sqlite3_exec FAILED: %s\n\t%s", sql.c_str(), sqlite3_errmsg(_sdb    ) );
        sqlite3_free(zErrMsg);
        return false;
    }
    
    return true;
}

bool pIoTServerDB::getMinMaxForValues(stringvector keys, double hours, vector<minMaxEntry_t> &entries){
    std::lock_guard<std::mutex> lock(_mutex);
    bool success = false;
    
    vector<minMaxEntry_t> results;
 
    for(string key: keys){
        
        if(_minMaxValues.count(key)){
            minMaxEntry_t entry;
            
            entry.key = key;
            
            if(_minMaxValues[key].getMin(entry.minValue)
               && _minMaxValues[key].getMax(entry.maxValue)){
                results.push_back(entry) ;
            }
        }
    }
    
    success = results.size() > 0;
    
    if(success){
        entries = results;
    }
    
    return success;
}

bool pIoTServerDB::currentValueDouble(string key, double &val) {
    bool success = false;
 
    if(_values.count(key)){
        auto lastpair = _values[key];
        
        // see if it's a number
        char *p1;
        double oldval = strtod(lastpair.second.c_str(), &p1);
        if(*p1 == 0 ){
            val = oldval;
            success = true;
        }
    }
    return success;
}


bool pIoTServerDB::removeHistoryForKey(string key, float days){
    
    std::lock_guard<std::mutex> lock(_mutex);
    bool success = false;
    
    string sql = string("DELETE FROM DEVICE_DATA ");
    
    if(key.size() > 0) {
        sql += "WHERE NAME ='" + key + "' ";
    }
    
    if(days > 0) {
        
        if(key.size() > 0) {
            sql += "AND ";
        }
        else {
            sql += "WHERE ";
        }
        
        sql += " AND datetime(DATE, 'auto') > datetime('now' , '-" + to_string(days) + " days', 'localtime')";
    }
    else {
        sql += ";";
    }
    sqlite3_stmt* stmt = NULL;
    
    if(sqlite3_prepare_v2(_sdb, sql.c_str(), -1,  &stmt, NULL)  == SQLITE_OK){
        
        if(sqlite3_step(stmt) == SQLITE_DONE) {
            
            int count =  sqlite3_changes(_sdb);
            LOGT_DEBUG("sqlite %s\n %d rows affected", sql.c_str(), count );
            success = true;
        }
        else
        {
            LOGT_ERROR("sqlite3_step FAILED: %s\n\t%s", sql.c_str(), sqlite3_errmsg(_sdb    ) );
        }
        sqlite3_finalize(stmt);
        
    }
    else {
        LOGT_ERROR("sqlite3_prepare_v2 FAILED: %s\n\t%s", sql.c_str(), sqlite3_errmsg(_sdb    ) );
        sqlite3_errmsg(_sdb);
    }
    
    return success;
}
 
bool pIoTServerDB::countHistoryForRange(string key, int &countOut){
    std::lock_guard<std::mutex> lock(_mutex);
    bool success = false;
    
    int count = 0;
    
    sqlite3_stmt* stmt = NULL;
    
    string sql = string("SELECT COUNT(*) FROM DEVICE_RANGE WHERE NAME = '")
    + key + "'";
    
    sqlite3_prepare_v2(_sdb, sql.c_str(), -1,  &stmt, NULL);
    
    if(sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
        success = true;
    }
    
    sqlite3_finalize(stmt);
    
    if(success){
        countOut = count;
    }
    
    return success;
}

bool pIoTServerDB::countHistoryForKey(string key, int &countOut){
    std::lock_guard<std::mutex> lock(_mutex);
    bool success = false;
    
    int count = 0;
    
    sqlite3_stmt* stmt = NULL;
    
    string sql = string("SELECT COUNT(*) FROM DEVICE_DATA WHERE NAME = '")
    + key + "'";
    
    sqlite3_prepare_v2(_sdb, sql.c_str(), -1,  &stmt, NULL);
    
    if(sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
        success = true;
    }
    
    sqlite3_finalize(stmt);
    
    if(success){
        countOut = count;
    }
    
    return success;
}

bool pIoTServerDB::historyForKey(string key, historicValues_t &valuesOut,
                                 int days, int limit, int offset){
    
    std::lock_guard<std::mutex> lock(_mutex);
    bool success = false;
    
    historicValues_t values;
    values.clear();
    
    string sql = string("SELECT  DATE, VALUE FROM DEVICE_DATA WHERE NAME = '")
    + key + "'";
    
    if(limit){
        sql += " ORDER BY DATE DESC LIMIT " + to_string(limit);
        
        if(offset)
            sql += " OFFSET " + to_string(offset);
     }
     else if(days > 0) {
         sql += " AND datetime(DATE, 'auto') > datetime('now' , '-" + to_string(days) + " days', 'localtime')";
    }
   
    sql += ";" ;
    
    sqlite3_stmt* stmt = NULL;
    
    sqlite3_prepare_v2(_sdb, sql.c_str(), -1,  &stmt, NULL);
    
    while ( (sqlite3_step(stmt)) == SQLITE_ROW) {
        time_t  when =  sqlite3_column_int64(stmt, 0);
        string  value = string((char*) sqlite3_column_text(stmt, 1));
        values.push_back(make_pair(when, value)) ;
    }
    sqlite3_finalize(stmt);
    
    success = values.size() > 0;
    
    if(success){
        valuesOut = values;
    }
     
    return success;
}


bool     pIoTServerDB::historyForRange(string key, historicRanges_t &historyOut,
                                       int days, int limit, int offset){
    std::lock_guard<std::mutex> lock(_mutex);
    bool success = false;
 
    historicRanges_t ranges;
    ranges.clear();
    
    string sql = string("SELECT DATE, MIN, MAX FROM DEVICE_RANGE WHERE NAME = '")
    + key + "'";
    
    if(limit){
        sql += " ORDER BY DATE DESC LIMIT " + to_string(limit);
        
        if(offset)
            sql += " OFFSET " + to_string(offset);
     }
     else if(days > 0) {
        sql += " AND datetime(DATE, 'auto') > datetime('now' , '-" + to_string(days) + " days', 'localtime')";
    }
   
    sql += ";" ;
 
    
    sqlite3_stmt* stmt = NULL;
    
    sqlite3_prepare_v2(_sdb, sql.c_str(), -1,  &stmt, NULL);
    
    while ( (sqlite3_step(stmt)) == SQLITE_ROW) {
        time_t  when =  sqlite3_column_int64(stmt, 0);
        double  min =   sqlite3_column_double(stmt, 1);
        double  max =   sqlite3_column_double(stmt, 2);
        ranges.push_back( make_tuple(when, min, max) ) ;
    }
    sqlite3_finalize(stmt);
    
    success = ranges.size() > 0;
    
    if(success){
        historyOut = ranges;
    }
    return success;
}

bool pIoTServerDB::removeHistoryForRange(string key, float days){

    std::lock_guard<std::mutex> lock(_mutex);
    bool success = false;
    
    string sql = string("DELETE FROM DEVICE_RANGE ");
    
    if(key.size() > 0) {
        sql += "WHERE NAME ='" + key + "' ";
    }
    
    if(days > 0) {
        
        if(key.size() > 0) {
            sql += "AND ";
        }
        else {
            sql += "WHERE ";
        }
        
        sql += " AND datetime(DATE, 'auto') > datetime('now' , '-" + to_string(days) + " days', 'localtime')";
    }
    else {
        sql += ";";
    }
    sqlite3_stmt* stmt = NULL;
    
    if(sqlite3_prepare_v2(_sdb, sql.c_str(), -1,  &stmt, NULL)  == SQLITE_OK){
        
        if(sqlite3_step(stmt) == SQLITE_DONE) {
            
            int count =  sqlite3_changes(_sdb);
            LOGT_DEBUG("sqlite %s\n %d rows affected", sql.c_str(), count );
            success = true;
        }
        else
        {
            LOGT_ERROR("sqlite3_step FAILED: %s\n\t%s", sql.c_str(), sqlite3_errmsg(_sdb    ) );
        }
        sqlite3_finalize(stmt);
        
    }
    else {
        LOGT_ERROR("sqlite3_prepare_v2 FAILED: %s\n\t%s", sql.c_str(), sqlite3_errmsg(_sdb    ) );
        sqlite3_errmsg(_sdb);
    }
    
    return success;
}


// MARK: -   SQL DATABASE Alerts

bool pIoTServerDB::logAlert(alert_t evt, string details, time_t when ){
    
    if(when == 0)
        when = time(NULL);
     
    bool hasDetails = details.size();
    
    //printf("%s \t EVT: %s\n", ts.ISO8601String().c_str(), displayStringForAlert(evt).c_str());
    
    string sql = string("INSERT INTO ALERT_LOG (ALERT,DATE") ;
    sql += hasDetails?",DETAILS": "";
    sql +=  ") VALUES  (" + to_string(evt)
    + ", '" + to_string(when) + "' ";
    sql += hasDetails? (", '" + details + "');") :");";
  
    char *zErrMsg = 0;
    if(sqlite3_exec(_sdb,sql.c_str(),NULL, 0, &zErrMsg  ) != SQLITE_OK){
        LOGT_ERROR("sqlite3_exec FAILED: %s\n\t%s", sql.c_str(), sqlite3_errmsg(_sdb    ) );
        sqlite3_free(zErrMsg);
        return false;
    }
    
    return true;
}

string pIoTServerDB::displayStringForAlert(alert_t evt){
    
    string result = "Unknown";
    switch (evt) {
            
        case ALERT_START:
            result = "Startup";
            break;
            
        case ALERT_SHUTDOWN:
            result = "Shutdown";
            break;
            
        case ALERT_MESSAGE:
            result = "Msg";
            break;
 
        case ALERT_ERROR:
            result = "Error";
            break;

        default:;
            
    }
    
    return result;
}

bool pIoTServerDB::countHistoryForAlerts(int &countOut){
    std::lock_guard<std::mutex> lock(_mutex);
    bool success = false;
    
    int count = 0;
    
    sqlite3_stmt* stmt = NULL;
    
    string sql = string("SELECT COUNT(*) FROM ALERT_LOG;");
  
    sqlite3_prepare_v2(_sdb, sql.c_str(), -1,  &stmt, NULL);
    
    if(sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
        success = true;
    }
    
    sqlite3_finalize(stmt);
    
    if(success){
        countOut = count;
    }
    
    return success;
    
}

bool pIoTServerDB::historyForAlerts( historicAlerts_t &alertsOut,
                                  float days, int limit, int offset){
    std::lock_guard<std::mutex> lock(_mutex);
    bool success = false;
    
    historicAlerts_t alerts;
    alerts.clear();
    
    string sql = string("SELECT strftime('%s', DATE) AS DATE, ALERT, DETAILS FROM ALERT_LOG ");
    
    if(limit){
        sql += " ORDER BY DATE DESC LIMIT " + to_string(limit);
        if(offset)
            sql += " OFFSET " + to_string(offset);
    }
    else if(days > 0) {
        sql += " AND datetime(DATE, 'auto') > datetime('now' , '-" + to_string(days) + " days', 'localtime')";
    }
  
    sql += ";";
    
    sqlite3_stmt* stmt = NULL;
    
    sqlite3_prepare_v2(_sdb, sql.c_str(), -1,  &stmt, NULL);
    
    while ( (sqlite3_step(stmt)) == SQLITE_ROW) {
        time_t  when    =  sqlite3_column_int64(stmt, 0);
        int  alert      =  sqlite3_column_int(stmt, 1);
        char* d =  (char*) sqlite3_column_text(stmt, 2);
        
        string details  =  d?string(d):string();
        alerts.push_back(make_tuple(when, (alert_t) alert, details)) ;
    }
    sqlite3_finalize(stmt);
    
    success =  true ; //events.size() > 0;
    
    if(success){
        alertsOut = alerts;
    }
    
    return success;
}

bool pIoTServerDB::removehistoryForAlerts(float days){
    
    std::lock_guard<std::mutex> lock(_mutex);
    bool success = false;
    
    string sql = string("DELETE FROM ALERT_LOG ");
    
    if(days > 0) {
        sql += " AND datetime(DATE, 'auto') > datetime('now' , '-" + to_string(days) + " days', 'localtime')";
    }
    else {
        sql += ";";
    }
    sqlite3_stmt* stmt = NULL;
    
    if(sqlite3_prepare_v2(_sdb, sql.c_str(), -1,  &stmt, NULL)  == SQLITE_OK){
        
        if(sqlite3_step(stmt) == SQLITE_DONE) {
            
            int count =  sqlite3_changes(_sdb);
            LOGT_DEBUG("sqlite %s\n %d rows affected", sql.c_str(), count );
            success = true;
        }
        else
        {
            LOGT_ERROR("sqlite3_step FAILED: %s\n\t%s", sql.c_str(), sqlite3_errmsg(_sdb    ) );
        }
        sqlite3_finalize(stmt);
        
    }
    else {
        LOGT_ERROR("sqlite3_prepare_v2 FAILED: %s\n\t%s", sql.c_str(), sqlite3_errmsg(_sdb    ) );
        sqlite3_errmsg(_sdb);
    }
    
    return success;
}


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


bool pIoTServerDB::sequenceFind(string name, sequenceID_t  sidOut){
    
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


//optional<reference_wrapper<Sequence>> pIoTServerDB::sequencesGetSequence(sequenceID_t sid){
//    
//    if(_sequences.count(sid) >0 ){
//        return  ref(_sequences[sid]);
//    }
//    
//    return optional<reference_wrapper<Sequence>> ();
//}

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
        {  // on completing the last step, reset it to zero
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
         
            // does the sequence have a valid trigger
            if(seq._trigger.shouldTriggerFromTimeEvent(solar, localNow)
               || seq.wasManuallyTriggered()){
                
                // has it started to run?
                if(seq._nextStepToRun  == 0){
                    sid.push_back( key);
                }
                else {
                    
                    // do we have to wait for the duration of the last step.
                    Step lastStepRun;
                    seq.getStep(seq._nextStepToRun -1, lastStepRun);
                    uint64_t lastDurration = lastStepRun.duration();
                    
                    if(localNow > (seq._lastStepRunTime + lastDurration)){
                        sid.push_back( key);
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
            // we dont set last run for app events
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
        
        // skip app events
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
    
    
    // event groups prevent us from running needless events when we reboot.
    // we only run the last elligable one for setting a steady state
    
    long nowMins = (localNow - solar.previousMidnight) / SECS_PER_MIN;
    
    for (const auto& [sgid, _] : _sequenceGroups) {
        sequenceGroupInfo_t* info  =  &_sequenceGroups[sgid];
        
        // create a map all all sequences that need to run
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
            // delete the last one
            seqMap.erase(prev(seqMap.end()));
            
            // mark the rest as ran
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


// MARK: - Utility

string pIoTServerDB::makeNonce(){
  
    u_long num;
    string nonce;
    
    std::uniform_int_distribution<long> distribution(0,999999);
    num = distribution(_rng);
  
    std::stringstream ss;
    ss << std::setw(6) << std::setfill('0') << num;

    nonce  = ss.str();
//    nonce = std::format("{:06}", num);
    
    return nonce;
}

