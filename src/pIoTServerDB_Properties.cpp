//
//  pIoTServerDB_Properties.cpp
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

// MARK: - properties
bool pIoTServerDB::setProperty(string key, string value){

    bool shouldUpdate =
    (_props.count(key) == 0)
    ||(_props[key] != value) ;

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


// NOTE THAT ALL CONFIG PROPERTIES ARE STRINGS-- YOU NEED TO CONVERT THEM YOURSELF.
//
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

        ifs.open(_propertyFilePath, ios::in);
        if(!ifs.is_open()) return false;

        _props.clear();
        ifs >> _props;

        statusOk = true;
        _didChangeProperties  = false;

        ifs.close();

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

            else if( it.key() == PROP_RULE && it.value().is_array()){
               for (auto& el : it.value()) {
                    if(el.is_object()){
                        Rule rule = Rule(el);
                        if(rule.isValid()&& ! ruleIDIsValid(rule._rawRuleID)){
                            _rules[rule._rawRuleID] = rule;
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

        _props.erase(PROP_SEQUENCE);
        _props.erase(PROP_SEQUENCE_GROUPS);
        _props.erase(PROP_RULE);

        refreshSolarEvents();

    }
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

                if(seq->hasCallBackAction()) continue;
                if(seq->isEphemeral()) continue;

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

        if(_rules.size() > 0){
            json j;
            for (const auto& [rid, _] : _rules) {
                Rule* rule =  &_rules[rid];
                j.push_back(rule->JSON());
            }
            jP[PROP_RULE] = j;
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
