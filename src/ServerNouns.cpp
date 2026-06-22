//
//  ServerNouns.cpp
//  demoServer
//
//  Created by Vincent Moscaritolo on 2/10/22.
//

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>

#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>

#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <ranges>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>


#include "ServerNouns.hpp"
#include "ServerCmdQueue.hpp"
#include "RESTServerConnection.hpp"
#include "ServerCmdQueue.hpp"

#include "ServerCmdValidators.hpp"
#include "pIoTServerMgr.hpp"
#include "IncidentMgr.hpp"
#include "TimeStamp.hpp"
#include "PropValKeys.hpp"
#include "LogMgr.hpp"
#include "Utils.hpp"

#include "I2C.hpp"
#include "W1_Device.hpp"

#include <sys/utsname.h>


static bool readDoubleFromFile(const std::string& path, double& valueOut) {
    std::ifstream ifs(path);

    if (!ifs.is_open()) {
        return false;
    }

    double value = 0.0;
    if (!(ifs >> value)) {
        return false;
    }

    valueOut = value;
    return true;
}

static bool readUInt8FromFile(const std::string& path, uint8_t& valueOut) {
    std::ifstream ifs(path);

    if (!ifs.is_open()) {
        return false;
    }

    unsigned int value = 0;
    if (!(ifs >> value)) {
        return false;
    }

    if (value > UINT8_MAX) {
        return false;
    }

    valueOut = static_cast<uint8_t>(value);
    return true;
}

static bool getCPUTemp(double& tempOut) {
    double milliC = 0.0;

    if (!readDoubleFromFile("/sys/class/thermal/thermal_zone0/temp", milliC)) {
        return false;
    }

    tempOut = milliC / 1000.0;
    return true;
}

static bool getFanState(uint8_t& stateOut) {
    return readUInt8FromFile(
        "/sys/class/thermal/cooling_device0/cur_state",
        stateOut
    );
}

static bool getSystemTimes(uint64_t& systemTimeOut,
                           uint64_t& systemBootTimeOut,
                           uint64_t& systemUptimeOut)
{
    struct sysinfo info {};

    if (sysinfo(&info) != 0) {
        return false;
    }

    time_t now = time(nullptr);

    if (now <= 0 || info.uptime < 0) {
        return false;
    }

    systemTimeOut = static_cast<uint64_t>(now);
    systemUptimeOut = static_cast<uint64_t>(info.uptime);
    systemBootTimeOut = systemTimeOut - systemUptimeOut;

    return true;
}

static std::string getOSPrettyName()
{
    std::ifstream ifs("/etc/os-release");

    if (!ifs.is_open()) {
        return "";
    }

    std::string line;
    while (std::getline(ifs, line)) {
        static const std::string key = "PRETTY_NAME=";

        if (line.rfind(key, 0) != 0) {
            continue;
        }

        std::string value = line.substr(key.size());

        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }

        return value;
    }

    return "";
}

// MARK: -  SCHEMA NOUN HANDLERS

static void Schema_NounHandler([[maybe_unused]] ServerCmdQueue* cmdQueue,
                               REST_URL url,
                               [[maybe_unused]] TCPClientInfo cInfo,
                               ServerCmdQueue::cmdCallback_t completion) {

    using namespace rest;
    json reply;

    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();
    auto queries = url.queries();

    // CHECK METHOD
    if(url.method() != HTTP_GET ) {
        (completion) (reply, STATUS_INVALID_METHOD);
        return;
    }

    auto path = url.path();
    string noun;

    if (path.size() == 1){

        if(queries.size() == 0){
            reply[string(JSON_ARG_SCHEMA)] = db->schemaJSON();
        }
        else {
            stringvector keys;
            json schemas;

            for (std::string const& key : std::views::keys(queries)){
                json entry = db->schemaJSON(key);
                if(!entry.is_null()){
                    schemas[key] = entry[key];
                }
            }
            reply[string(JSON_ARG_SCHEMA)] = schemas;
        }
    }
    else  if (path.size() == 2){
        string key = path.at(1);
        std::transform(key.begin(), key.end(), key.begin(), ::toupper);
        reply[string(JSON_ARG_SCHEMA)] = db->schemaJSON(key);
    }

    makeStatusJSON(reply,STATUS_OK);
    (completion) (reply, STATUS_OK);
};


// MARK: - HISTORY NOUN HANDLER

static bool History_NounHandler_GET([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                    REST_URL url,
                                    [[maybe_unused]] TCPClientInfo cInfo,
                                    ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    json reply;
    ServerCmdArgValidator v1;
    auto path = url.path();

    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    size_t pathsize = path.size();

    int days = 0;
    int limit = 0;
    int offset = 0;
    string str;

    if(v1.getStringFromMap(JSON_HDR_LIMIT, url.headers(), str)){
        char* p;
        limit = (int) strtol(str.c_str(), &p, 10);
        //        if(*p != 0) days = 0;
    }

    if(v1.getStringFromMap(JSON_HDR_DAYS, url.headers(), str)){
        char* p;
        days =  (int) strtol(str.c_str(), &p, 10);
        //      if(*p != 0) days = 0;
    }

    if(v1.getStringFromMap(JSON_HDR_OFFSET, url.headers(), str)){
        char* p;
        offset = (int) strtol(str.c_str(), &p, 10);
        //       if(*p != 0) days = 0;
    }

    if(pathsize == 2){

        string key = path.at(1);
        std::transform(key.begin(), key.end(), key.begin(), ::toupper);

        pIoTServerDB::valueSchema_t schema =  db->schemaForKey(key);

        if(schema.tracking == TR_TRACK_CHANGES
           || schema.tracking == TR_TRACK_LATEST_VALUE) {

            pIoTServerDB::historicValues_t history;

            reply[PROP_KEY] = key;
            reply[JSON_ARG_UNITS] = stringforSchemaUnits(schema.units);
            reply[PROP_TITLE] =  schema.title;
            string deviceID;
            if(pIoTServer->getDeviceIDForKey(key, deviceID)){
                reply[PROP_DEVICE_ID] =  deviceID;
            }

            if(db->historyForKey(key, history, days, limit, offset)){
                json j;
                for (auto& entry : history) {

                    json j1;
                    j1[string(JSON_ARG_VALUE)]         =  entry.second;
                    j1[string(JSON_ARG_TIME)]         =   entry.first;
                    j.push_back(j1);
                }

                reply[string(JSON_ARG_VALUES)] = j;

            }
        }
        else if(schema.tracking == TR_TRACK_RANGE){

            pIoTServerDB::historicRanges_t ranges;

            reply[PROP_KEY] = key;
            reply[JSON_ARG_UNITS] = stringforSchemaUnits(schema.units);
            reply[PROP_TITLE] =  schema.title;

            string deviceID;
            if(pIoTServer->getDeviceIDForKey(key, deviceID)){
                reply[PROP_DEVICE_ID] =  deviceID;
            }

            if(db->historyForRange( key, ranges, days, limit, offset)){
                json j;
                for (auto& entry : ranges) {

                    json j1;
                    j1[string(JSON_ARG_TIME)]         = get<0>(entry);
                    j1[string(JSON_PROP_MIN)]         = get<1>(entry);
                    j1[string(JSON_PROP_MAX)]         =  get<2>(entry);
                    j.push_back(j1);
                }

                reply[string(JSON_ARG_RANGE)] = j;
            }
        }
        if(reply.empty()){
            makeStatusJSON(reply, STATUS_BAD_REQUEST, "Not Found", "The value key provided has no history.");
            (completion) (reply, STATUS_BAD_REQUEST);
            return true;
        }
    }
    else if(pathsize == 3){
        string subpath =  path.at(1);

        if( subpath == SUBPATH_COUNT) {
            string key = path.at(2);
            std::transform(key.begin(), key.end(), key.begin(), ::toupper);

            int count;

            pIoTServerDB::valueSchema_t schema =  db->schemaForKey(key);

            if(schema.tracking == TR_TRACK_CHANGES
               || schema.tracking == TR_TRACK_LATEST_VALUE) {

                if(db->countHistoryForKey(key,  count)){
                    reply[JSON_ARG_COUNT] = count;
                }
            }
            else if(schema.tracking == TR_TRACK_RANGE){
                if(db->countHistoryForRange(key,  count)){
                    reply[JSON_ARG_COUNT] = count;
                }
            }
        }

        if(reply.empty()){
            makeStatusJSON(reply, STATUS_BAD_REQUEST, "URL Invalid", "The value key provided was malformed or null");
            (completion) (reply, STATUS_BAD_REQUEST);
            return true;
        }
    }

    makeStatusJSON(reply,STATUS_OK);
    (completion) (reply, STATUS_OK);
    return true;
}

static bool History_NounHandler_DELETE([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                       REST_URL url,
                                       [[maybe_unused]] TCPClientInfo cInfo,
                                       ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    json reply;
    ServerCmdArgValidator v1;
    string str;

    auto path = url.path();
    string noun;

    if(path.size() > 0) {
        noun = path.at(0);
    }

    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    // CHECK sub paths
    map<string ,string> propList;

    float days = 0;

    if(v1.getStringFromMap(JSON_HDR_DAYS, url.headers(), str)){
        char* p;
        days =  strtof(str.c_str(), &p);
        if(*p != 0) days = 0;
    }

    if (path.size() == 1){
        db->removeHistoryForRange(string(), days);
        db->removeHistoryForKey(string(), days);
        makeStatusJSON(reply,STATUS_NO_CONTENT);
        (completion) (reply, STATUS_NO_CONTENT);
        return true;

    }
    else if (path.size() == 2){
        string key = path.at(1);
        std::transform(key.begin(), key.end(), key.begin(), ::toupper);

        pIoTServerDB::valueSchema_t schema =  db->schemaForKey(key);
        if(schema.tracking == TR_TRACK_CHANGES
           || schema.tracking == TR_TRACK_LATEST_VALUE) {
            if(db->removeHistoryForKey(key, days)){
                makeStatusJSON(reply,STATUS_NO_CONTENT);
                (completion) (reply, STATUS_NO_CONTENT);
                return true;
            }
        }
        else if(schema.tracking == TR_TRACK_RANGE){
            if(db->removeHistoryForRange(key, days)){
                makeStatusJSON(reply,STATUS_NO_CONTENT);
                (completion) (reply, STATUS_NO_CONTENT);
                return true;
            }
        }
    }
    return false;
}

static void History_NounHandler([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                REST_URL url,
                                [[maybe_unused]] TCPClientInfo cInfo,
                                ServerCmdQueue::cmdCallback_t completion) {

    using namespace rest;
    json reply;

    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();
    string noun;

    bool isValidURL = false;

    if(path.size() > 0) {
        noun = path.at(0);
    }

    switch(url.method()){
        case HTTP_GET:
            isValidURL = History_NounHandler_GET(cmdQueue,url,cInfo, completion);
            break;

            //        case HTTP_PUT:
            //            isValidURL = History_NounHandler_PUT(cmdQueue,url,cInfo, completion);
            //            break;

            //        case HTTP_PATCH:
            //            isValidURL = History_NounHandler_PATCH(cmdQueue,url,cInfo, completion);
            //            break;

            //        case HTTP_POST:
            //            isValidURL = History_NounHandler_POST(cmdQueue,url,cInfo, completion);
            //            break;
            //
        case HTTP_DELETE:
            isValidURL = History_NounHandler_DELETE(cmdQueue,url,cInfo, completion);
            break;

        default:
            (completion) (reply, STATUS_INVALID_METHOD);
            return;
    }

    if(!isValidURL) {
        (completion) (reply, STATUS_NOT_FOUND);
    }
};


// MARK: -  PROPERTIES NOUN HANDLER

static bool Properties_NounHandler_GET([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                       REST_URL url,
                                       [[maybe_unused]] TCPClientInfo cInfo,
                                       ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;

    json reply;

    auto path = url.path();
    auto queries = url.queries();

    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    json allProps;
    json propEntries;

    auto filter = { PROP_CONFIG };

    /*
     * /props
     * /props?ui.groups
     * /props?ui.groups&devices
     * /props/ui.groups
     */

    if(path.size() == 1) {
        db->getAllProperties(filter, &allProps);

        if(queries.empty()) {
            propEntries = allProps;
        }
        else {
            for(std::string const& propName : std::views::keys(queries)) {
                if(allProps.contains(propName)) {
                    propEntries[propName] = allProps[propName];
                }
            }

            if(propEntries.empty()) {
                makeStatusJSON(reply,
                               STATUS_BAD_REQUEST,
                               "URL Invalid",
                               "No matching property keys were found");
                completion(reply, STATUS_BAD_REQUEST);
                return true;
            }
        }
    }
    else if(path.size() == 2) {
        db->getAllProperties(filter, &allProps);

        string propName = path.at(1);

        if(allProps.contains(propName)) {
            propEntries[propName] = allProps[propName];
        }
        else {
            makeStatusJSON(reply,
                           STATUS_BAD_REQUEST,
                           "URL Invalid",
                           "The property key provided was not found");
            completion(reply, STATUS_BAD_REQUEST);
            return true;
        }
    }
    else {
        makeStatusJSON(reply,
                       STATUS_BAD_REQUEST,
                       "URL Invalid",
                       "The property URL was malformed");
        completion(reply, STATUS_BAD_REQUEST);
        return true;
    }

    reply[string(JSON_ARG_PROPERTIES)] = propEntries;

    makeStatusJSON(reply, STATUS_OK);
    completion(reply, STATUS_OK);

    return true;
}


static bool Properties_NounHandler_PUT([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                       REST_URL url,
                                       [[maybe_unused]] TCPClientInfo cInfo,
                                       ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();
    auto body     = url.body();

    json reply;

    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    string noun;

    if(path.size() > 0) {
        noun = path.at(0);
    }

    bool didUpdate = false;
    auto filter = {PROP_CONFIG} ;

    for(auto it =  body.begin(); it != body.end(); ++it) {
        string key = Utils::trim(it.key());

        // cant change filtered items
        auto found = std::find(filter.begin(), filter.end(), key);
        if (found != filter.end()) {
            return false;
        }

        if(key == PROP_CONFIG_LOGFILE_FLAGS   // flags are hex byte
           && it.value().is_string()){

            ServerCmdArgValidator v1;
            uint8_t logFlags;

            if(v1.getByteFromJSON(PROP_CONFIG_LOGFILE_FLAGS, url.body(), logFlags)){
                LogMgr::shared()->_logFlags = logFlags;
                db->setConfigProperty(string(PROP_CONFIG_LOGFILE_FLAGS),  to_hex <unsigned char>(logFlags,true));
                didUpdate = true;
            }
        }
        else if(key == PROP_CONFIG_LOGFILE_PATH
                && it.value().is_string()){

            string logfile_path = Utils::trim(it.value());

            bool success = LogMgr::shared()->setLogFilePath(logfile_path);
            if(success){
                db->setConfigProperty(string(PROP_CONFIG_LOGFILE_PATH), logfile_path);
                didUpdate = true;
            }
            else {
                string lastError =  string("Failed to set logfile path. Error: ") + to_string(errno);
                string lastErrorString = string(::strerror(errno));

                makeStatusJSON(reply, STATUS_BAD_REQUEST, lastError, lastErrorString, key );;
                (completion) (reply, STATUS_BAD_REQUEST);
                return true;
            }
        }
        else if(it.value().is_string()){
            string value = Utils::trim(it.value());

            if(db->setProperty(key, value)){
                didUpdate = true;
            }
        }
        else if(it.value().is_number()){
            string value =to_string(it.value());

            if(db->setProperty(key, value)){
                didUpdate = true;
            }
        }
        else if(it.value().is_boolean()){
            string value =  it.value()?"1":"0";

            if(db->setProperty(key, value)){
                didUpdate = true;
            }

        } else if(body[it.key()].is_null()){
            // delete property
            if(db->removeProperty( key)){
                didUpdate = true;
            }
        }
    }

    if(didUpdate){
     // do nothing I guess
    }

    makeStatusJSON(reply,STATUS_OK);
    (completion) (reply, STATUS_OK);
    return true;
}


static bool Properties_NounHandler_DELETE([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                          REST_URL url,
                                          [[maybe_unused]] TCPClientInfo cInfo,
                                          ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    json reply;

    auto path = url.path();
    string noun;

    if(path.size() > 0) {
        noun = path.at(0);
    }

    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    if (path.size() == 2){
        string propName = path.at(1);

        auto filter = {PROP_CONFIG} ;
        // cant change filtered items
        auto found = std::find(filter.begin(), filter.end(), propName);
        if (found != filter.end()) {
            return false;
        }

        auto configProps = {PROP_CONFIG_LOGFILE_FLAGS,PROP_CONFIG_LOGFILE_PATH} ;
        auto f1 = std::find(configProps.begin(), configProps.end(), propName);
        if (f1 != filter.end()) {
            if(!db->removeConfigProperty(propName)){
                return false;
            }
        }
        else if(!db->removeProperty(propName)){
            return false;
        }
    }
    else {
        return false;
    }

    // CHECK sub paths

    makeStatusJSON(reply,STATUS_NO_CONTENT);
    (completion) (reply, STATUS_NO_CONTENT);
    return true;
}




static void Properties_NounHandler([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                   REST_URL url,
                                   [[maybe_unused]] TCPClientInfo cInfo,
                                   ServerCmdQueue::cmdCallback_t completion) {

    using namespace rest;
    json reply;

    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();
    string noun;

    bool isValidURL = false;

    if(path.size() > 0) {
        noun = path.at(0);
    }

    switch(url.method()){
        case HTTP_GET:
            isValidURL = Properties_NounHandler_GET(cmdQueue,url,cInfo, completion);
            break;

        case HTTP_PUT:
            isValidURL = Properties_NounHandler_PUT(cmdQueue,url,cInfo, completion);
            break;

            //        case HTTP_POST:
            //            isValidURL = Properties_NounHandler_POST(cmdQueue,url,cInfo, completion);
            //            break;
            //
        case HTTP_DELETE:
            isValidURL = Properties_NounHandler_DELETE(cmdQueue,url,cInfo, completion);
            break;

        default:
            (completion) (reply, STATUS_INVALID_METHOD);
            return;
    }

    if(!isValidURL) {
        (completion) (reply, STATUS_NOT_FOUND);
    }
};


// MARK: - LOG - NOUN HANDLER

static bool Log_NounHandler_PATCH([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                  REST_URL url,
                                  [[maybe_unused]] TCPClientInfo cInfo,
                                  ServerCmdQueue::cmdCallback_t completion) {

    using namespace rest;
    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();

    ServerCmdArgValidator v1;

    json reply;

    string str;

    if(v1.getStringFromJSON(JSON_ARG_MESSAGE, url.body(), str)){

        LogMgr::shared()->logTimedStampString(str);
        makeStatusJSON(reply,STATUS_OK);
        (completion) (reply, STATUS_OK);
        return true;
    }

    return false;
}

static void Log_NounHandler([[maybe_unused]] ServerCmdQueue* cmdQueue,
                            REST_URL url,  // entire request
                            [[maybe_unused]] TCPClientInfo cInfo,
                            ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    json reply;

    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();
    string noun;

    bool isValidURL = false;

    if(path.size() > 0) {
        noun = path.at(0);
    }

    switch(url.method()){
        case HTTP_GET:
            //         isValidURL = Log_NounHandler_GET(cmdQueue,url,cInfo, completion);
            break;

        case HTTP_PUT:
            //         isValidURL = Log_NounHandler_PUT(cmdQueue,url,cInfo, completion);
            break;

        case HTTP_PATCH:
            isValidURL = Log_NounHandler_PATCH(cmdQueue,url,cInfo, completion);
            break;

            //        case HTTP_POST:
            //            isValidURL = Log_NounHandler_POST(cmdQueue,url,cInfo, completion);
            //            break;
            //
            //        case HTTP_DELETE:
            //            isValidURL = Log_NounHandler_DELETE(cmdQueue,url,cInfo, completion);
            //            break;

        default:
            (completion) (reply, STATUS_INVALID_METHOD);
            return;
    }

    if(!isValidURL) {
        (completion) (reply, STATUS_NOT_FOUND);
    }
}



// MARK: -  STATE NOUN HANDLERS

static bool State_NounHandler_GET([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                  REST_URL url,
                                  [[maybe_unused]] TCPClientInfo cInfo,
                                  ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    using namespace timestamp;

    auto pIoTServer = pIoTServerMgr::shared();
    //   auto db = pIoTServer->getDB();

    json reply;

    auto path = url.path();
    string noun;

    if(path.size() > 0) {
        noun = path.at(0);
    }

    // CHECK sub paths
    if(noun != NOUN_STATE){
        (completion) (reply, STATUS_NOT_FOUND);
        return false;
    }

    json propEntries;
    pIoTServer->getAllDeviceStatus(propEntries);
    reply[ string(JSON_ARG_DEVICES) ] = propEntries;

    reply[string(JSON_ARG_DATE)] = TimeStamp().RFC1123String();
    reply[string(JSON_ARG_UPTIME)]    = pIoTServer->upTime();
    reply[string(JSON_ARG_ENABLE)] = pIoTServer->isRunning();

    makeStatusJSON(reply,STATUS_OK);
    (completion) (reply, STATUS_OK);
    return true;
}


static bool State_NounHandler_PUT([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                  REST_URL url,
                                  [[maybe_unused]] TCPClientInfo cInfo,
                                  ServerCmdQueue::cmdCallback_t completion) {

    using namespace rest;
    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();

    ServerCmdArgValidator v1;
    auto pIoTServer = pIoTServerMgr::shared();
     //   auto db = pIoTServer->getDB();

    json reply;


    if(path.size() == 1) {

        bool  enable = false;
        if(v1.getBoolFromJSON(JSON_ARG_ENABLE, url.body(), enable)){

            if(XOR(enable, pIoTServer->isRunning())){
                if(enable)
                    pIoTServer->start();
                else
                    pIoTServer->stop();

                makeStatusJSON(reply,STATUS_OK);
                (completion) (reply, STATUS_OK);


            }else {

                string lastError =  string("Server In wrong state");
                string lastErrorString =  string("Server was ")
                +  (pIoTServer->isRunning()?"Running":"Stopped");

                makeStatusJSON(reply, STATUS_BAD_REQUEST, lastError, lastErrorString );
                (completion) (reply, STATUS_BAD_REQUEST);
            }
        }
        else
        {
            makeStatusJSON(reply, STATUS_BAD_REQUEST, "URL Invalid", "The value key provided was malformed or null");
            (completion) (reply, STATUS_BAD_REQUEST);

        }
        return true;
    }

    if(path.size() == 2) {

        string subpath =   path.at(1);
        string str;

        //        if( subpath == SUBPATH_PUMP) {
        //
        //
        //            if(v1.getStringFromJSON(JSON_ARG_STATE, url.body(), str)){
        //                std::transform(str.begin(), str.end(), str.begin(), ::tolower);
        //
        //                if(str == JSON_VAL_ON){
        //
        //                    makeStatusJSON(reply,STATUS_OK);
        //                    (completion) (reply, STATUS_OK);
        //                    return true;
        //
        //                }else  if(str == JSON_VAL_OFF){
        //
        //                    makeStatusJSON(reply,STATUS_OK);
        //                    (completion) (reply, STATUS_OK);
        //                    return true;
        //
        //                }else  if(str == JSON_VAL_AUTO){
        //
        //                    makeStatusJSON(reply,STATUS_OK);
        //                    (completion) (reply, STATUS_OK);
        //                    return true;
        //                }
        //
        //            }
        //        }
    }

    return false;
}


static void State_NounHandler([[maybe_unused]] ServerCmdQueue* cmdQueue,
                              REST_URL url,
                              [[maybe_unused]] TCPClientInfo cInfo,
                              ServerCmdQueue::cmdCallback_t completion) {

    using namespace rest;
    json reply;

    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();
    string noun;

    bool isValidURL = false;

    if(path.size() > 0) {
        noun = path.at(0);
    }

    switch(url.method()){
        case HTTP_GET:
            isValidURL = State_NounHandler_GET(cmdQueue,url,cInfo, completion);
            break;

        case HTTP_PUT:
            isValidURL = State_NounHandler_PUT(cmdQueue,url,cInfo, completion);
            break;

            //        case HTTP_PATCH:
            //            isValidURL = State_NounHandler_PATCH(cmdQueue,url,cInfo, completion);
            //            break;
            //
            //        case HTTP_POST:
            //            isValidURL = State_NounHandler_POST(cmdQueue,url,cInfo, completion);
            //            break;
            //
            //        case HTTP_DELETE:
            //            isValidURL = Alerts_NounHandler_DELETE(cmdQueue,url,cInfo, completion);
            //            break;

        default:
            (completion) (reply, STATUS_INVALID_METHOD);
            return;
    }

    if(!isValidURL) {
        (completion) (reply, STATUS_NOT_FOUND);
    }
};

// MARK: -  VALUES NOUN HANDLERS

static bool Values_NounHandler_GET([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                   REST_URL url,
                                   [[maybe_unused]] TCPClientInfo cInfo,
                                   ServerCmdQueue::cmdCallback_t completion) {

    using namespace rest;
    json reply;
    ServerCmdArgValidator v1;

    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    auto path = url.path();
    auto queries = url.queries();
    string noun;

    if(path.size() > 0) {
        noun = path.at(0);
    }

    // CHECK sub paths
    if(noun != NOUN_VALUES){
        (completion) (reply, STATUS_NOT_FOUND);
        return false;
    }

    if(path.size() == 1) {
        string str;
        json j;

        if(queries.size() == 0){

            eTag_t eTag = 0;

            if(v1.getStringFromMap("if-none-match", url.headers(), str)){
                char* p;
                eTag = strtol(str.c_str(), &p, 0);
                if(*p != 0) eTag = 0;
            }

            reply[string(JSON_ARG_VALUES)] = db->currentValuesJSON(eTag);
            reply[string(JSON_ARG_ETAG)] = db->lastEtag();
            reply[string(JSON_ARG_MANUAL_KEYS)] = db->keysInManualMode();

            std::set<std::string> autoKeys;
            if( db->allKeysInAllSequences(autoKeys)){
                reply[string(JSON_ARG_AUTOMATIC_KEYS)] = autoKeys;
            }
        }
        else {
            stringvector keys;
            json schedules;

              for (std::string const& key : std::views::keys(queries)){
                keys.push_back(key);

                json entry = db->scheduleForValue(key);
                if(!entry.is_null()){
                    schedules[key] = entry;
                }
            }

            if(!schedules.is_null()){
                reply[string(JSON_ARG_SCHEDULE)] = schedules;
            }
            reply[string(JSON_ARG_VALUES)] = db->currentJSONForKeys(keys);
         }

        makeStatusJSON(reply,STATUS_OK);
        (completion) (reply, STATUS_OK);
    }
    else if(path.size() == 2) {
        json j;

        string key = path.at(1);
        std::transform(key.begin(), key.end(), key.begin(), ::toupper);

        reply[string(JSON_ARG_VALUES)] = db->currentJSONForKey(key);

        makeStatusJSON(reply,STATUS_OK);
        (completion) (reply, STATUS_OK);
    }
    return true;
};

static bool Values_NounHandler_PUT([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                   REST_URL url,
                                   [[maybe_unused]] TCPClientInfo cInfo,
                                   ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();
    auto body     = url.body();

    json reply;

    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    string noun;

    if(path.size() > 0) {
        noun = path.at(0);
    }

    bool success = false;
    keyValueMap_t   kv;

    for(auto it =  body.begin(); it != body.end(); ++it) {
        string key = Utils::trim(it.key());
        string value = JSON_value_toString(body[it.key()]);

        // sanity check.
        if(value.empty()) continue;

        if(db->unitsForKey(key) == valueSchemaUnits_t::BOOL){

            bool autoRequested = caseInSensStringCompare(value, string(JSON_VAL_AUTO));

            /* check if key is listed in a sequence  */
            std::set<std::string> autoKeys;
            db->allKeysInAllSequences(autoKeys);

            if(autoKeys.count(key)){

                /* if the key is in manual mode and we set it to auto
                 then remove it from the manual key list */

                if(db->isKeyInManualMode(key) && autoRequested){
                    success = db->setKeyManualMode(key, false);
                }

                /* if we try and set it to true or false,
                 then it gets added to the manual list */
                if(!autoRequested){
                    db->setKeyManualMode(key, true);
                    kv[key] = value;
                }
            }
            else
            {
                if(!autoRequested) {
                    // just set it if it not in any sequence
                    kv[key] = value;
                 }
            }
        }
        else {
            kv[key] = value;
        }
    }
//   #warning DEBUG
//       {
//           for(auto &[k,v] : kv){
//               cout << "SET(" << k << ", " << v << ")" << endl;
//           }
//          }
    if(kv.size())
    {
        success =  pIoTServer->setValues(kv);
    }
    else {

        makeStatusJSON(reply, STATUS_NOT_MODIFIED);
        (completion) (reply, STATUS_NOT_MODIFIED);
        return true;
    }

    if(!success){
        makeStatusJSON(reply, STATUS_BAD_REQUEST, "URL Invalid", "value could not be set");
        (completion) (reply, STATUS_BAD_REQUEST);
        return true;
    }

    makeStatusJSON(reply,STATUS_OK);
    (completion) (reply, STATUS_OK);
    return true;
}

static void Values_NounHandler([[maybe_unused]] ServerCmdQueue* cmdQueue,
                               REST_URL url,  // entire request
                               [[maybe_unused]] TCPClientInfo cInfo,
                               ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    json reply;

    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();
    string noun;

    bool isValidURL = false;

    if(path.size() > 0) {
        noun = path.at(0);
    }

    switch(url.method()){
        case HTTP_GET:
            isValidURL = Values_NounHandler_GET(cmdQueue,url,cInfo, completion);
            break;

        case HTTP_PUT:
            isValidURL =   Values_NounHandler_PUT(cmdQueue,url,cInfo, completion);
            break;

        default:
            (completion) (reply, STATUS_INVALID_METHOD);
            return;
    }

    if(!isValidURL) {
        (completion) (reply, STATUS_NOT_FOUND);
    }
}
// MARK: -  VALUES RANGE NOUN HANDLERS

static bool ValuesRange_NounHandler_GET([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                   REST_URL url,
                                   [[maybe_unused]] TCPClientInfo cInfo,
                                        ServerCmdQueue::cmdCallback_t completion) {

    using namespace rest;
    json reply;
    ServerCmdArgValidator v1;

    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    auto path = url.path();
    auto queries = url.queries();

    string str;
    double hours = 0;

    if(v1.getStringFromMap(JSON_HDR_HOURS, url.headers(), str)){
        char* p;
        hours =  strtod(str.c_str(), &p);
        if(*p != 0) hours = 0;
    }

    stringvector keys;

    if(path.size() == 1) {
        for (std::string const& key : std::views::keys(queries)){
            keys.push_back(key);
        }
    }
    else if(path.size() == 2) {
        json j;

        string key = path.at(1);
        std::transform(key.begin(), key.end(), key.begin(), ::toupper);
        keys.push_back(key);
    }

    if(keys.size() == 0){
        makeStatusJSON(reply, STATUS_BAD_REQUEST, "URL Invalid", "The value key provided was malformed or null");
        (completion) (reply, STATUS_BAD_REQUEST);
        return true;
    }

    vector<pIoTServerDB::minMaxEntry_t> entries;

    db->getMinMaxForValues(keys, hours, entries);


    json results;
    for(auto entry : entries){
        json e;
        e[JSON_PROP_MAX] = entry.maxValue;
        e[JSON_PROP_MIN] = entry.minValue;
        results[ entry.key] = e;
    }

    reply[string(JSON_ARG_VALUES)] = results;

    makeStatusJSON(reply,STATUS_OK);
    (completion) (reply, STATUS_OK);

    return true;
};



static void ValuesRange_NounHandler([[maybe_unused]] ServerCmdQueue* cmdQueue,
                               REST_URL url,  // entire request
                               [[maybe_unused]] TCPClientInfo cInfo,
                               ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    json reply;

    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();
    string noun;

    bool isValidURL = false;

    if(path.size() > 0) {
        noun = path.at(0);
    }

    switch(url.method()){
        case HTTP_GET:
            isValidURL = ValuesRange_NounHandler_GET(cmdQueue,url,cInfo, completion);
            break;

        default:
            (completion) (reply, STATUS_INVALID_METHOD);
            return;
    }

    if(!isValidURL) {
        (completion) (reply, STATUS_NOT_FOUND);
    }
}

// MARK: - Date - NOUN HANDLER

static void Date_NounHandler([[maybe_unused]] ServerCmdQueue* cmdQueue,
                             REST_URL url,
                             [[maybe_unused]] TCPClientInfo cInfo,
                             ServerCmdQueue::cmdCallback_t completion) {

    using namespace rest;
    using namespace timestamp;
    auto pIoTServer = pIoTServerMgr::shared();

    json reply;

    // CHECK METHOD
    if(url.method() != HTTP_GET ) {
        (completion) (reply, STATUS_INVALID_METHOD);
        return;
    }

    auto path = url.path();
    string noun;

    if(path.size() > 0) {
        noun = path.at(0);
    }

    // CHECK sub paths
    if(noun != NOUN_DATE){
        (completion) (reply, STATUS_NOT_FOUND);
        return;
    }

    reply[string(JSON_ARG_DATE)] = TimeStamp().RFC1123String();
    reply[string(JSON_ARG_UPTIME)]    = pIoTServer->upTime();

    solarTimes_t solar;
    if( pIoTServer->getSolarEvents(solar)) {
        json sj;
        sj[JSON_ARG_SOLAR_CIVIL_SUNRISE] = solar.civilSunRiseMins;
        sj[JSON_ARG_SOLAR_SUNRISE] = solar.sunriseMins;
        sj[JSON_ARG_SOLAR_SUNSET] = solar.sunSetMins;
        sj[JSON_ARG_SOLAR_CIVIL_SUNSET] = solar.civilSunSetMins;
        sj[JSON_ARG_SOLAR_LATITUDE] = solar.latitude;
        sj[JSON_ARG_SOLAR_LONGITUDE] = solar.longitude;
        sj[JSON_ARG_SOLAR_GMTOFFSET] = (solar.gmtOffset / 3600);
        sj[JSON_ARG_SOLAR_TIMEZONE] = solar.timeZoneString;
        sj[JSON_ARG_SOLAR_MIDNIGHT] = solar.previousMidnight - solar.gmtOffset;

        sj[JSON_ARG_SOLAR_POM_VISIBLE] = solar.moonVisable;
        sj[JSON_ARG_SOLAR_POM_STR] = solar.moonPhaseName;
        sj[JSON_ARG_SOLAR_POM_PHASE] = solar.moonPhase;
        reply[JSON_ARG_SOLAR] = sj;
    }

    makeStatusJSON(reply,STATUS_OK);
    (completion) (reply, STATUS_OK);
}

// MARK: - version - NOUN HANDLER

static uint64_t getDiskFreeMB(const std::string& path = "/") {
    struct statvfs st {};

    if (statvfs(path.c_str(), &st) != 0) {
        return 0;
    }

    uint64_t freeBytes =
        static_cast<uint64_t>(st.f_bavail) *
        static_cast<uint64_t>(st.f_frsize);

    return freeBytes / (1024ULL * 1024ULL);
}

static double getDiskUsedPercent(const std::string& path = "/")
{
    struct statvfs st {};

    if (statvfs(path.c_str(), &st) != 0) {
        return 0.0;
    }

    uint64_t total =
        (uint64_t)st.f_blocks * (uint64_t)st.f_frsize;

    uint64_t free =
        (uint64_t)st.f_bavail * (uint64_t)st.f_frsize;

    return 100.0 * (double)(total - free) / (double)total;
}


static std::string getPrimaryIPv4Address() {
    struct ifaddrs* ifaddr = nullptr;

    if (getifaddrs(&ifaddr) == -1) {
        return "";
    }

    std::string result;

    for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) {
            continue;
        }

        if (ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        if ((ifa->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }

        char addrbuf[INET_ADDRSTRLEN] = {0};

        auto* sa = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);

        if (inet_ntop(AF_INET, &sa->sin_addr, addrbuf, sizeof(addrbuf)) == nullptr) {
            continue;
        }

        result = addrbuf;
        break;
    }

    freeifaddrs(ifaddr);
    return result;
}

static bool getCPUinfo(map<string,string> &info){
    bool didSucceed = false;
    info.clear();

    stringvector filter = {"Serial" , "Model"} ;

    try{
        std::ifstream   ifs;
        ifs.open("/proc/cpuinfo", ios::in);
        if( ifs.is_open()){

            string line;
            while (std::getline(ifs, line)) {

                for(string item : filter){
                    if (line.find(item) != std::string::npos) {
                        string value =  line.substr(line.find(":") + 1);
                        info[item] = value;
                        break;
                    };
                }
                if(info.size() == filter.size())
                {
                    didSucceed = true;
                    break;
                }
            }
            ifs.close();
        }
    }
    catch(std::ifstream::failure &err) {
    }
    return didSucceed;
}


static void Version_NounHandler([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                REST_URL url,
                                [[maybe_unused]] TCPClientInfo cInfo,
                                ServerCmdQueue::cmdCallback_t completion) {

    using namespace rest;
    using namespace timestamp;

    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    json reply;

    // CHECK METHOD
    if (url.method() != HTTP_GET) {
        completion(reply, STATUS_INVALID_METHOD);
        return;
    }

    const uint64_t appUptime =
        static_cast<uint64_t>(pIoTServer->upTime());

    reply[string(JSON_ARG_UPTIME)] = appUptime;          // Legacy: app uptime
    reply[string(JSON_ARG_VERSION)] = pIoTServerMgr::pIoTServerMgr_Version;
    reply[string(JSON_ARG_BUILD_TIME)] = string(__DATE__) + " " + string(__TIME__);

    uint64_t systemTime = 0;
    uint64_t systemBootTime = 0;
    uint64_t systemUptime = 0;

    if (getSystemTimes(systemTime, systemBootTime, systemUptime)) {
        const uint64_t safeAppUptime = std::min(appUptime, systemUptime);

        reply["system_time"] = systemTime;
        reply["system_boot_time"] = systemBootTime;
        reply["system_uptime"] = systemUptime;

        reply[string(JSON_ARG_UPTIME)] = safeAppUptime;
        reply["app_start_time"] = systemTime - safeAppUptime;
    }

    string instanceName;
    if (db->getConfigProperty(string(JSON_ARG_NAME), instanceName)) {
        reply[string(JSON_ARG_NAME)] = instanceName;
    }

    string description;
    if (db->getConfigProperty(string(PROP_DESCRIPTION), description)) {
        reply[string(PROP_DESCRIPTION)] = description;
    }

    std::string procname;
    std::ifstream("/proc/self/comm") >> procname;
    if (!procname.empty()) {
        reply[string(JSON_ARG_SERVER_PROCNAME)] = procname;
    }

    std::string ipAddress = getPrimaryIPv4Address();
    if (!ipAddress.empty()) {
        reply[JSON_ARG_IP_V4ADDR] = ipAddress;
    }

    reply[JSON_ARG_IP_PORT] = db->getRESTPort();

    if (std::filesystem::exists(".dockerenv")) {
        reply["DOCKER"] = true;
    }

    double tempC = 0.0;
    if (getCPUTemp(tempC)) {
        reply[VAL_CPU_TEMPERATURE] = tempC;
    }

    uint8_t fanState = 0;
    if (getFanState(fanState)) {
        reply[VAL_CPU_FAN] = fanState;
    }

    reply["disk_free_root_mb"] = getDiskFreeMB("/");
    reply["disk_used_pct"] = getDiskUsedPercent("/");

    map<string, string> info = {};
    if (getCPUinfo(info) && !info.empty()) {
        for (auto& [key, val] : info) {
            reply["cpu." + key] = val;
        }
    }

    std::string prettyName = getOSPrettyName();
    if (!prettyName.empty()) {
        reply[string(JSON_ARG_OS_VERSION)] = prettyName;
    }

    struct utsname buffer {};
    if (uname(&buffer) == 0) {
        reply[string(JSON_ARG_OS_NODENAME)] = string(buffer.nodename);
        reply[string(JSON_ARG_OS_RELEASE)] = string(buffer.release);
        reply[string(JSON_ARG_OS_MACHINE)] = string(buffer.machine);
    }

    makeStatusJSON(reply, STATUS_OK);
    completion(reply, STATUS_OK);
}

// MARK: -  DEVICE NOUN HANDLERS



static bool Device_NounHandler_GET([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                   REST_URL url,
                                   [[maybe_unused]] TCPClientInfo cInfo,
                                   ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    json reply;

    auto path = url.path();
    auto queries = url.queries();

    string noun;

    if(path.size() > 0) {
        noun = path.at(0);
    }

    auto pIoTServer = pIoTServerMgr::shared();

    json propEntries;

    if(path.size() == 1){
        if(queries.size() == 0){
            pIoTServer->getAllDeviceProperties(propEntries);
        }
        else {
            // asking for specific devices

            for (std::string const& deviceID : std::views::keys(queries)){
                json j;
                if(pIoTServer->getDeviceProperties(deviceID, j)){
                    propEntries[deviceID]= j;
                }
            }
        }

    }

    else if(path.size() == 2) {
        json j;
        string deviceID = path.at(1);

        if(!pIoTServer->getDeviceProperties(deviceID, propEntries)) {
            makeStatusJSON(reply, STATUS_BAD_REQUEST, "URL Invalid", "The value key provided was malformed or null");
            (completion) (reply, STATUS_BAD_REQUEST);
            return true;

        }
    }
    else {
        makeStatusJSON(reply, STATUS_BAD_REQUEST, "URL Invalid", "The value key provided was malformed or null");
        (completion) (reply, STATUS_BAD_REQUEST);
        return true;
    }

    reply[ string(JSON_ARG_DEVICES) ] = propEntries;

    makeStatusJSON(reply,STATUS_OK);
    (completion) (reply, STATUS_OK);
    return true;
}


static bool Device_NounHandler_PUT([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                   REST_URL url,
                                   [[maybe_unused]] TCPClientInfo cInfo,
                                   ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();
    auto body     = url.body();

    json reply;

    auto pIoTServer = pIoTServerMgr::shared();
    //   auto db = pIoTServer->getDB();

    string noun;

    if(path.size() > 0) {
        noun = path.at(0);
    }

    if(path.size() != 2){
        makeStatusJSON(reply, STATUS_BAD_REQUEST, "URL Invalid", "no device was specified");
        (completion) (reply, STATUS_BAD_REQUEST);
        return true;
    }

    string deviceID = path.at(1);

    if(!pIoTServer->setDeviceProperties(deviceID, body)) {
        makeStatusJSON(reply, STATUS_BAD_REQUEST, "URL Invalid", "Device not found, or could not be set");
        (completion) (reply, STATUS_BAD_REQUEST);
        return true;
    }

    makeStatusJSON(reply,STATUS_OK);
    (completion) (reply, STATUS_OK);
    return true;
}
static void Device_NounHandler([[maybe_unused]] ServerCmdQueue* cmdQueue,
                               REST_URL url,  // entire request
                               [[maybe_unused]] TCPClientInfo cInfo,
                               ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    json reply;

    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();
    string noun;

    bool isValidURL = false;

    if(path.size() > 0) {
        noun = path.at(0);
    }

    switch(url.method()){
        case HTTP_GET:
            isValidURL = Device_NounHandler_GET(cmdQueue,url,cInfo, completion);
            break;

        case HTTP_PUT:
            isValidURL =   Device_NounHandler_PUT(cmdQueue,url,cInfo, completion);
            break;

        default:
            (completion) (reply, STATUS_INVALID_METHOD);
            return;
    }

    if(!isValidURL) {
        (completion) (reply, STATUS_NOT_FOUND);
    }
}



// MARK: -  SEQUENCES NOUN HANDLERS



static bool Sequence_NounHandler_GET([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                     REST_URL url,
                                     [[maybe_unused]] TCPClientInfo cInfo,
                                     ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;

    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();
    json reply;

    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    // create a sorted vector of timed events
    vector<std::pair<string, int16_t>> timedSeq;
    solarTimes_t solar;
    pIoTServer->getSolarEvents(solar);

    time_t now = time(NULL);
    struct tm* tm = localtime(&now);
    time_t localNow  = (now + tm->tm_gmtoff);

    vector<sequenceID_t> fSIDS  = db->sequencesInTheFuture(solar, localNow);
    vector<sequenceID_t> sids;

    bool justRunning = false;

    if(queries.size() == 0){
        sids = db->allSequenceIDs();
    }
    else if (queries.size() == 1 &&
             caseInSensStringCompare(queries.begin()->first, "running")) {
        sids = db->allSequenceIDs();
        justRunning = true;
    }
    else {
        for (std::string const& key : std::views::keys(queries)){
            sequenceID_t sid;
            if(str_to_SequenceID(key.c_str(), &sid))
                sids.push_back(sid);
        }
    }

    json allSequences;
    json cronSequences;
    json futureSequences;
    json timedSequences;
    json runningSequences;

    for(auto sid : sids){
        json js = db->sequenceJSON(sid);
        if(!js.is_null()){

            bool isRunning = db->sequenceIsRunning(sid);
            js[PROP_ISRUNNING] = isRunning;

            if(isRunning) {
                uint stepNo;
                if(db->sequenceCurrentStep(sid, stepNo)){
                    js[JSON_ARG_STEP] =  stepNo;
                    }
             }

            if(db->sequenceIsAborting(sid)){
                js[JSON_ARG_ABORT]= true;
            }

            string sidStr = to_hex<unsigned short>(sid);
            allSequences[sidStr] = js;

            if(isRunning)
                 runningSequences.push_back(sidStr);

            if(db->sequenceisEnable(sid)){
                EventTrigger trig;
                db->sequenceGetTrigger(sid, trig);
                if(trig.isCronEvent()){

                    time_t nextRun;
                    if( trig.nextCronTime(nextRun)){
                        cronSequences[sidStr] = nextRun;
                    }
                }
                else if(trig.isTimed()){

                    int16_t minsFromMidnight = 0;
                    if(trig.calculateTriggerTime(solar,minsFromMidnight)) {
                        timedSequences[sidStr] = minsFromMidnight;
                    }
                }

                if(std::find(fSIDS.begin(), fSIDS.end(), sid)!=fSIDS.end()){
                    futureSequences.push_back(sidStr);
                }

            }
        }
    }

    if(justRunning){
        reply[string(JSON_ARG_RUNNING_SEQUENCES)] = runningSequences;
    }
    else {
        reply[string(JSON_ARG_SEQUENCE_IDS)] = allSequences;
        reply[string(JSON_ARG_CRON_SEQUENCES)] = cronSequences;
        reply[string(JSON_ARG_FUTURE_SEQUENCES)] = futureSequences;
        reply[string(JSON_ARG_TIMED_SEQUENCES)] = timedSequences;
        reply[string(JSON_ARG_RUNNING_SEQUENCES)] = runningSequences;
    }

    makeStatusJSON(reply,STATUS_OK);
    (completion) (reply, STATUS_OK);
    return false;
}


static bool Sequence_NounHandler_DELETE([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                        REST_URL url,
                                        [[maybe_unused]] TCPClientInfo cInfo,
                                        ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();

    json reply;

    ServerCmdArgValidator v1;
    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    sequenceID_t sid;

    if( !str_to_SequenceID(path.at(1).c_str(), &sid) || !db->sequenceIDIsValid(sid))
        return false;

    if(path.size() == 2) {
        if(db->sequenceDelete(sid)){
            makeStatusJSON(reply,STATUS_NO_CONTENT);
            (completion) (reply, STATUS_NO_CONTENT);
        }
        else {
            reply[string(JSON_ARG_SEQUENCE_ID)] = to_hex<unsigned short>(sid);
            makeStatusJSON(reply, STATUS_BAD_REQUEST, "Delete Failed" );;
            (completion) (reply, STATUS_BAD_REQUEST);
        }
        return true;

    }
    return false;
}


static bool Sequence_NounHandler_POST([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                      REST_URL url,
                                      [[maybe_unused]] TCPClientInfo cInfo,
                                      ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();

    json reply;

    ServerCmdArgValidator v1;

    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    if(path.size() == 1) {
        // Create event

        sequenceID_t sid = 0;

        Sequence seq = Sequence(url.body());
        if(seq.isValid()
           && db->sequenceSave(seq, &sid)) {
            reply[string(JSON_ARG_SEQUENCE_ID)] = SequenceID_to_string(sid);
            reply[string(JSON_ARG_NAME)] = seq.name();
            makeStatusJSON(reply,STATUS_OK);
            (completion) (reply, STATUS_OK);
            return true;
        }

        makeStatusJSON(reply, STATUS_BAD_REQUEST, "Create Sequence Failed" );;
        (completion) (reply, STATUS_BAD_REQUEST);
        return true;
    }

    return false;
}


static bool Sequence_NounHandler_PATCH([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                       REST_URL url,
                                       [[maybe_unused]] TCPClientInfo cInfo,
                                       ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;

    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();

    json reply;

    ServerCmdArgValidator v1;
    auto pIoTServer = pIoTServerMgr::shared();
     auto db = pIoTServer->getDB();

       if(path.size() == 2) {
            sequenceID_t sid;

           if( !str_to_SequenceID(path.at(1).c_str(), &sid)
              || !db->sequenceIDIsValid(sid))
               return false;

           bool failed = false;
           bool success = false;

           bool  abort = false;
           if(v1.getBoolFromJSON(JSON_ARG_ABORT, url.body(), abort)){

               if(pIoTServer->abortSequence(sid)){
                reply[string(JSON_ARG_SEQUENCE_ID)] = to_hex<unsigned short>(sid);
                reply[JSON_ARG_ABORT] = true;
                success = true;
               }
               else
               {
                   failed = true;
               }
           }
           else {

               // set name
               string newName;
               if(v1.getStringFromJSON(JSON_ARG_NAME, url.body(), newName)){
                   if(db->sequenceSetName(sid, newName)) {
                       reply[string(JSON_ARG_SEQUENCE_ID)] = to_hex<unsigned short>(sid);
                       reply[string(JSON_ARG_NAME)] = newName;
                       success = true;
                   }
                   else {
                       failed = true;
                   }
               }
        //
               // set Description
               string newDescr;
               if(v1.getStringFromJSON(PROP_DESCRIPTION, url.body(), newDescr)){
                   if(db->sequenceSetDescription(sid, newDescr)) {
                       reply[string(JSON_ARG_SEQUENCE_ID)] = to_hex<unsigned short>(sid);
                       reply[string(PROP_DESCRIPTION)] = newDescr;
                       success = true;
                   }
                   else {
                       failed = true;
                   }
               }

               bool  enable = false;
               if(v1.getBoolFromJSON(JSON_ARG_ENABLE, url.body(), enable)){
                   if(db->sequenceSetEnable(sid, enable)) {
                       reply[string(JSON_ARG_SEQUENCE_ID)] = to_hex<unsigned short>(sid);
                       reply[JSON_ARG_ENABLE] = enable;
                       success = true;
                   }
                   else {
                       failed = true;
                   }
               }

           }

           if(success) {
               makeStatusJSON(reply,STATUS_OK);
               (completion) (reply, STATUS_OK);
               return true;
           }

           if(failed){
               reply[string(JSON_ARG_SEQUENCE_ID)] = to_hex<unsigned short>(sid);
               makeStatusJSON(reply, STATUS_BAD_REQUEST, "Update Failed" );;
               (completion) (reply, STATUS_BAD_REQUEST);
               return true;
           }

           reply[string(JSON_ARG_SEQUENCE_ID)] = to_hex<unsigned short>(sid);
           makeStatusJSON(reply, STATUS_BAD_REQUEST,
                          "Body Invalid",
                          "Body missing argument",
                          url.pathString());
           (completion) (reply, STATUS_BAD_REQUEST);

           return true;
     }
    return false;
}


static bool Sequence_NounHandler_PUT([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                     REST_URL url,
                                     [[maybe_unused]] TCPClientInfo cInfo,
                                     ServerCmdQueue::cmdCallback_t completion) {

    using namespace rest;
    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();
    json reply;

    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    ServerCmdArgValidator v1;

    if(path.size() == 1) {

        sequenceID_t sid;
        string str;

        if(v1.getStringFromJSON(JSON_ARG_SEQUENCE_ID, url.body(), str)){

            if( !str_to_SequenceID(str.c_str(), &sid) || !db->sequenceIDIsValid(sid))
                return false;

            if(db->sequenceIsRunning(sid) || db->sequenceIsAborting(sid)) {
                reply[string(JSON_ARG_SEQUENCE_ID)] = SequenceID_to_string(sid);
                makeStatusJSON(reply, STATUS_CONFLICT, "Conflict", "Sequence is already running.");
                (completion) (reply, STATUS_CONFLICT);
                return true;
            }

            bool queued = pIoTServer->startRunningSequence(sid);

            if(queued){
                reply[string(JSON_ARG_SEQUENCE_ID)] = SequenceID_to_string(sid);

                makeStatusJSON(reply,STATUS_OK);
                (completion) (reply, STATUS_OK);
            }
            else{
                makeStatusJSON(reply, STATUS_BAD_REQUEST, "URL Invalid", "The value key provided was malformed or null");
                (completion) (reply, STATUS_BAD_REQUEST);

            }
            return true;
        }

    }
    makeStatusJSON(reply, STATUS_BAD_REQUEST,
                   "Body Invalid",
                   "Body missing argument",
                   url.pathString());
    (completion) (reply, STATUS_BAD_REQUEST);

    return true;

}

static void Sequences_NounHandler([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                  REST_URL url,
                                  [[maybe_unused]] TCPClientInfo cInfo,
                                  ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    json reply;

    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();
    string noun;

    bool isValidURL = false;

    if(path.size() > 0) {
        noun = path.at(0);
    }

    switch(url.method()){
        case HTTP_GET:
            isValidURL = Sequence_NounHandler_GET(cmdQueue,url,cInfo, completion);
            break;

        case HTTP_PUT:
            isValidURL = Sequence_NounHandler_PUT(cmdQueue,url,cInfo, completion);
            break;

        case HTTP_PATCH:
            isValidURL = Sequence_NounHandler_PATCH(cmdQueue,url,cInfo, completion);
            break;

        case HTTP_POST:
            isValidURL = Sequence_NounHandler_POST(cmdQueue,url,cInfo, completion);
            break;

        case HTTP_DELETE:
            isValidURL = Sequence_NounHandler_DELETE(cmdQueue,url,cInfo, completion);
            break;

        default:
            (completion) (reply, STATUS_INVALID_METHOD);
            return;
    }

    if(!isValidURL) {
        (completion) (reply, STATUS_NOT_FOUND);
    }

}


// MARK: -  SEQUENCE GROUPS NOUN HANDLERS

static bool SequenceGroup_NounHandler_NounHandler_GET([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                                      REST_URL url,
                                                      [[maybe_unused]] TCPClientInfo cInfo,
                                                      ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;

    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();
    json reply;

    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    // GET /event.groups
    if(path.size() == 1) {

        json groupsList;
        vector<sequenceGroupID_t> groupIDs;

        if(queries.size() == 0){
            groupIDs = db->allSequenceGroupIDs();
        }
        else
        {
            for (std::string const& key : std::views::keys(queries)){
                sequenceGroupID_t gid;
                if(str_to_SequenceGroupID(key.c_str(), &gid))
                    groupIDs.push_back(gid);
            }
        }

        for(auto groupID : groupIDs){
            json entry;

            entry[string(JSON_ARG_NAME)] =  db->sequenceGroupGetName(groupID);
            groupsList[ SequenceGroupID_to_string(groupID)] = entry;
        }

        reply[string(PROP_ARG_GROUPIDS)] = groupsList;
        makeStatusJSON(reply,STATUS_OK);
        (completion) (reply, STATUS_OK);
        return true;

    }
    // GET /event.groups/XXXX
    else if(path.size() == 2) {

        sequenceGroupID_t groupID;

        if( !str_to_SequenceGroupID(path.at(1).c_str(), &groupID) || !db->sequenceGroupIsValid(groupID))
            return false;

        reply[string(PROP_ARG_GROUPID)] = SequenceGroupID_to_string(groupID);
        reply[string(JSON_ARG_NAME)] =  db->sequenceGroupGetName(groupID);
        auto sids = db->sequenceGroupGetSequenceIDs(groupID);

        vector<string> ids;
        for (auto sid : sids) {
            ids.push_back(SequenceID_to_string(sid));
        }
        reply[string(JSON_ARG_SEQUENCE_IDS)] = ids;

        makeStatusJSON(reply,STATUS_OK);
        (completion) (reply, STATUS_OK);

    }
    else {

    }

    return false;
}

static bool SequenceGroup_NounHandler_POST([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                           REST_URL url,
                                           [[maybe_unused]] TCPClientInfo cInfo,
                                           ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();

    json reply;

    ServerCmdArgValidator v1;
    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    if(path.size() == 1) {

        string name;
        // Create group
        if(v1.getStringFromJSON(JSON_ARG_NAME, url.body(), name)){

            sequenceGroupID_t groupID;
            if(db->sequenceGroupFind(name, &groupID)){
                name = db->sequenceGroupGetName(groupID);
            }
            else {
                if (! db->sequenceGroupCreate(&groupID, name)) {
                    reply[string(JSON_ARG_NAME)] = name;
                    makeStatusJSON(reply, STATUS_BAD_REQUEST, "Set Failed" );;
                    (completion) (reply, STATUS_BAD_REQUEST);
                    return true;
                }
            }

            reply[string(PROP_ARG_GROUPID)] = SequenceGroupID_to_string(groupID);
            reply[string(JSON_ARG_NAME)] = name;
            makeStatusJSON(reply,STATUS_OK);
            (completion) (reply, STATUS_OK);
            return true;
        }
    }

    return false;
}


static bool SequenceGroup_NounHandler_DELETE([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                             REST_URL url,
                                             [[maybe_unused]] TCPClientInfo cInfo,
                                             ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();

    json reply;

    ServerCmdArgValidator v1;
    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    sequenceGroupID_t groupID;

    if(path.size() < 1) {
        return false;
    }

    if( !str_to_SequenceGroupID(path.at(1).c_str(), &groupID)
       || !db->sequenceGroupIsValid(groupID))
        return false;

    if(path.size() == 2) {
        if(db->sequenceGroupDelete(groupID)){
            makeStatusJSON(reply,STATUS_NO_CONTENT);
            (completion) (reply, STATUS_NO_CONTENT);
        }
        else {
            reply[string(PROP_ARG_GROUPID)] = SequenceGroupID_to_string(groupID);
            makeStatusJSON(reply, STATUS_BAD_REQUEST, "Delete Failed" );;
            (completion) (reply, STATUS_BAD_REQUEST);
        }
        return true;
    }
    else if(path.size() == 3) {

        sequenceID_t sid;

        if( !str_to_SequenceID(path.at(2).c_str(), &sid)
           || !db->sequenceIDIsValid(sid))
            return false;

        if(db->sequenceGroupRemoveSequence(groupID, sid)){
            makeStatusJSON(reply,STATUS_NO_CONTENT);
            (completion) (reply, STATUS_NO_CONTENT);
        }
        else {
            reply[string(PROP_ARG_GROUPID)] = SequenceGroupID_to_string(groupID);
            reply[string(JSON_ARG_SEQUENCE_ID)] = SequenceID_to_string(sid);
            makeStatusJSON(reply, STATUS_BAD_REQUEST, "Delete Failed" );;
            (completion) (reply, STATUS_BAD_REQUEST);
        }
    }
    return false;
}

static bool SequenceGroup_NounHandler_PATCH([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                            REST_URL url,
                                            [[maybe_unused]] TCPClientInfo cInfo,
                                            ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;

    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();

    json reply;

    ServerCmdArgValidator v1;
    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    sequenceGroupID_t groupID;

    if(path.size() < 1) {
        return false;
    }

    if( !str_to_SequenceGroupID(path.at(1).c_str(), &groupID)
       || !db->sequenceGroupIsValid(groupID))
        return false;


    if(path.size() == 2) {
        string name;
        // set name
        if(v1.getStringFromJSON(JSON_ARG_NAME, url.body(), name)){
            if(db->sequenceGroupSetName(groupID, name)) {
                reply[string(PROP_ARG_GROUPID)] =  SequenceGroupID_to_string(groupID);
                reply[string(JSON_ARG_NAME)] = name;

                makeStatusJSON(reply,STATUS_OK);
                (completion) (reply, STATUS_OK);
            }
            else {
                reply[string(PROP_ARG_GROUPID)] =  SequenceGroupID_to_string(groupID);
                makeStatusJSON(reply, STATUS_BAD_REQUEST, "Set Failed" );;
                (completion) (reply, STATUS_BAD_REQUEST);
            }
        }
    }
    return false;

}

static bool SequenceGroup_NounHandler_PUT([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                          REST_URL url,
                                          [[maybe_unused]] TCPClientInfo cInfo,
                                          ServerCmdQueue::cmdCallback_t completion) {

    using namespace rest;
    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();
    json reply;

    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    ServerCmdArgValidator v1;

    sequenceGroupID_t groupID;

    if(path.size() < 1) {
        return false;
    }

    if( !str_to_SequenceGroupID(path.at(1).c_str(), &groupID)
       || !db->sequenceGroupIsValid(groupID))
        return false;


    string str;
    if(v1.getStringFromJSON(JSON_ARG_SEQUENCE_ID, url.body(), str)){
        sequenceID_t sid;

        if( ! str_to_SequenceID(str.c_str(), &sid) || !db->sequenceIDIsValid(sid))
            return false;

        if(db->sequenceGroupAddSequence(groupID, sid)){
            reply[string(PROP_ARG_GROUPID)] = SequenceGroupID_to_string(groupID);
            reply[string(JSON_ARG_SEQUENCE_ID)] = SequenceID_to_string(sid);
            makeStatusJSON(reply,STATUS_OK);
            (completion) (reply, STATUS_OK);

        }
        else {
            reply[string(PROP_ARG_GROUPID)] = SequenceGroupID_to_string(groupID);
            reply[string(JSON_ARG_SEQUENCE_ID)] = SequenceID_to_string(sid);

            makeStatusJSON(reply, STATUS_BAD_REQUEST, "Set Failed" );;
            (completion) (reply, STATUS_BAD_REQUEST);
        }
        return  true;
    }

    return false;
}

static void SequenceGroups_NounHandler([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                       REST_URL url,
                                       [[maybe_unused]] TCPClientInfo cInfo,
                                       ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    json reply;

    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();
    string noun;

    bool isValidURL = false;

    if(path.size() > 0) {
        noun = path.at(0);
    }


    //    // is server available?
    //    if(!insteon.serverAvailable()) {
    //        makeStatusJSON(reply, STATUS_UNAVAILABLE, "Server is unavailable" );;
    //        (completion) (reply, STATUS_UNAVAILABLE);
    //        return;
    //    }

    switch(url.method()){
        case HTTP_GET:
            isValidURL = SequenceGroup_NounHandler_NounHandler_GET(cmdQueue,url,cInfo, completion);
            break;

        case HTTP_PUT:
            isValidURL = SequenceGroup_NounHandler_PUT(cmdQueue,url,cInfo, completion);
            break;

        case HTTP_PATCH:
            isValidURL = SequenceGroup_NounHandler_PATCH(cmdQueue,url,cInfo, completion);
            break;

        case HTTP_POST:
            isValidURL = SequenceGroup_NounHandler_POST(cmdQueue,url,cInfo, completion);
            break;

        case HTTP_DELETE:
            isValidURL = SequenceGroup_NounHandler_DELETE(cmdQueue,url,cInfo, completion);
            break;

        default:
            (completion) (reply, STATUS_INVALID_METHOD);
            return;
    }

    if(!isValidURL) {
        (completion) (reply, STATUS_NOT_FOUND);
    }

}

// MARK: -  TEST NOUN HANDLERS

static void Test_NounHandler([[maybe_unused]] ServerCmdQueue* cmdQueue,
                             REST_URL url,
                             [[maybe_unused]] TCPClientInfo cInfo,
                             ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    json reply;

    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();
    string noun;

 //   auto pIoTServer = pIoTServerMgr::shared();
    //  auto db = pIoTServer->getDB();


    if(path.size() > 0) {
        noun = path.at(0);
    }

    bool success = true;
    int error = -5;


    //    success = db->apiSecretCreate("foo", "bar" );
    //
    //    success = db->apiSecretSetSecret("foo",db->makeNonce() );
    //
    //    success = db->apiSecretDelete("foo");

    if(success) {
        makeStatusJSON(reply,STATUS_OK);
        (completion) (reply, STATUS_OK);
    }
    else
    {
        string lastError =  string("Test Failed, Error: ") + to_string(error);
        string lastErrorString = string(::strerror(error));

        makeStatusJSON(reply, STATUS_BAD_REQUEST, lastError, lastErrorString );
        (completion) (reply, STATUS_BAD_REQUEST);
    }

}


// MARK: -  PING NOUN HANDLERS

static void Ping_NounHandler([[maybe_unused]] ServerCmdQueue* cmdQueue,
                             REST_URL url,
                             [[maybe_unused]] TCPClientInfo cInfo,
                             ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    json reply;

    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();
    string noun;

   auto pIoTServer = pIoTServerMgr::shared();

   bool success = false;

   if(path.size() == 1) {
       noun = path.at(0);

       uint64_t systemTime = 0;
       uint64_t systemBootTime = 0;
       uint64_t systemUptime = 0;

       const uint64_t appUptime =
           static_cast<uint64_t>(pIoTServer->upTime());

       reply[string(JSON_ARG_UPTIME)] = appUptime;

       if (getSystemTimes(systemTime, systemBootTime, systemUptime)) {
           reply["system_time"] = systemTime;
           reply["system_uptime"] = systemUptime;
       }

       success = true;
    }



    if(success) {
        makeStatusJSON(reply,STATUS_OK);
        (completion) (reply, STATUS_OK);
    }
    else
    {
        int error = -5;

        string lastError =  string("Ping Failed, Error: ") + to_string(error);
        string lastErrorString = string(::strerror(error));

        makeStatusJSON(reply, STATUS_BAD_REQUEST, lastError, lastErrorString );
        (completion) (reply, STATUS_BAD_REQUEST);
    }

}

// MARK: -  INCIDENTS NOUN HANDLER

static bool Incidents_NounHandler_GET([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                      REST_URL url,
                                      [[maybe_unused]] TCPClientInfo cInfo,
                                      ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;

    json reply;
    ServerCmdArgValidator v1;
    auto path = url.path();

    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    size_t pathsize = path.size();

    if(pathsize == 1){
        pIoTServerDB::historicIncidents_t incidents;
        string str;

        float days = 0;
        int limit = 0;
        int offset = 0;
        int64_t sinceEtag = 0;
        bool activeOnly = false;

        if(v1.getStringFromMap(JSON_HDR_LIMIT, url.headers(), str)){
            char* p;
            limit = (int) strtol(str.c_str(), &p, 10);
            if(*p != 0) limit = 0;
        }

        if(v1.getStringFromMap(JSON_HDR_DAYS, url.headers(), str)){
            char* p;
            days = strtof(str.c_str(), &p);
            if(*p != 0) days = 0;
        }

        if(v1.getStringFromMap(JSON_HDR_OFFSET, url.headers(), str)){
            char* p;
            offset = (int) strtol(str.c_str(), &p, 10);
            if(*p != 0) offset = 0;
        }

        if(v1.getStringFromMap("etag", url.headers(), str)){
            char* p;
            sinceEtag = strtoll(str.c_str(), &p, 10);
            if(*p != 0) sinceEtag = 0;
        }

        if(v1.getStringFromMap("active", url.headers(), str)){
            bool b = false;
            if(stringToBool(str, b)) {
                activeOnly = b;
            }
        }

        int64_t currentEtag = 0;
        if(db->incidentGetEtag(currentEtag)) {
            reply["etag"] = currentEtag;
        }

        if(db->historyForIncidents(incidents, days, limit, offset, activeOnly, sinceEtag)){

            json j = json::array();

            for(auto &entry : incidents) {
                json j1;

                j1["id"]            = get<0>(entry);
                j1["source"]        = get<1>(entry);
                j1["code"]          = get<2>(entry);
                j1["key"]           = get<3>(entry);
                j1["severity"]      = get<4>(entry);
                j1["active"]        = get<5>(entry);
                j1["first_time"]    = get<6>(entry);
                j1["last_time"]     = get<7>(entry);
                j1["cleared_time"]  = get<8>(entry);
                j1["count"]         = get<9>(entry);
                j1["etag"]          = get<10>(entry);

                string message = get<11>(entry);
                string details = get<12>(entry);

                if(message.size()) {
                    j1["message"] = message;
                }

                if(details.size()) {
                    j1["details"] = details;
                }

                j.push_back(j1);
            }

            reply["incidents"] = j;
        }
    }
    else if(pathsize == 2){
        string subpath = path.at(1);

        if(subpath == SUBPATH_COUNT){

            string str;
            bool activeOnly = false;

            if(v1.getStringFromMap("active", url.headers(), str)){
                bool b = false;
                if(stringToBool(str, b)) {
                    activeOnly = b;
                }
            }

            int count = 0;

            if(db->countHistoryForIncidents(count, activeOnly)){
                reply[JSON_ARG_COUNT] = count;
            }
        }
        else if(subpath == "etag"){

            int64_t etag = 0;

            if(db->incidentGetEtag(etag)){
                reply["etag"] = etag;
            }
        }
        else if(subpath == "active"){

            pIoTServerDB::historicIncidents_t incidents;
            int64_t currentEtag = 0;

            if(db->incidentGetEtag(currentEtag)) {
                reply["etag"] = currentEtag;
            }

            if(db->historyForIncidents(incidents, 0, 0, 0, true, 0)){

                json j = json::array();

                for(auto &entry : incidents) {
                    json j1;

                    j1["id"]            = get<0>(entry);
                    j1["source"]        = get<1>(entry);
                    j1["code"]          = get<2>(entry);
                    j1["key"]           = get<3>(entry);
                    j1["severity"]      = get<4>(entry);
                    j1["active"]        = get<5>(entry);
                    j1["first_time"]    = get<6>(entry);
                    j1["last_time"]     = get<7>(entry);
                    j1["cleared_time"]  = get<8>(entry);
                    j1["count"]         = get<9>(entry);
                    j1["etag"]          = get<10>(entry);

                    string message = get<11>(entry);
                    string details = get<12>(entry);

                    if(message.size()) {
                        j1["message"] = message;
                    }

                    if(details.size()) {
                        j1["details"] = details;
                    }

                    j.push_back(j1);
                }

                reply["incidents"] = j;
            }
        }
        else
        {
            makeStatusJSON(reply, STATUS_BAD_REQUEST,
                           "URL Invalid",
                           "Unknown incidents subpath",
                           url.pathString());

            (completion)(reply, STATUS_BAD_REQUEST);
            return true;
        }
    }
    else
    {
        makeStatusJSON(reply, STATUS_BAD_REQUEST,
                       "URL Invalid",
                       "The request takes no arguments",
                       url.pathString());

        (completion)(reply, STATUS_BAD_REQUEST);
        return true;
    }

    makeStatusJSON(reply, STATUS_OK);
    (completion)(reply, STATUS_OK);
    return true;
}

static bool Incidents_NounHandler_PATCH([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                        REST_URL url,
                                        [[maybe_unused]] TCPClientInfo cInfo,
                                        ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;

    json reply;
    ServerCmdArgValidator v1;

    string message;
    string source = "SYSTEM";
    string code = "LOG_MESSAGE";
    string key = "piotserver";
    string details;

    if(v1.getStringFromJSON(JSON_ARG_MESSAGE, url.body(), message)){

        v1.getStringFromJSON("source", url.body(), source);
        v1.getStringFromJSON("code", url.body(), code);
        v1.getStringFromJSON("key", url.body(), key);
        v1.getStringFromJSON("details", url.body(), details);

        bool ok = IncidentMgr::shared()->notice(
            source,
            code,
            key,
            message.empty() ? nullptr : message.c_str(),
            details.empty() ? nullptr : details.c_str()
        );

        if(ok) {
            makeStatusJSON(reply, STATUS_OK);
            (completion)(reply, STATUS_OK);
            return true;
        }
    }

    makeStatusJSON(reply, STATUS_BAD_REQUEST,
                   "Invalid Incident",
                   "PATCH requires a message",
                   url.pathString());

    (completion)(reply, STATUS_BAD_REQUEST);
    return true;
}

static bool Incidents_NounHandler_DELETE([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                         REST_URL url,
                                         [[maybe_unused]] TCPClientInfo cInfo,
                                         ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;

    json reply;
    ServerCmdArgValidator v1;
    string str;

    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    /*
     * DELETE /incidents
     *
     * Supported cleanup modes:
     *
     *   X-PIOT-Incident-Cleanup: previous_runs
     *      Remove all incident history older than the most recent SERVER_START
     *      notice. This intentionally removes old active incidents too, because
     *      they belong to a previous server run and should not survive as
     *      current-run truth.
     *
     *   scope: previous_runs
     *      Legacy/local fallback for the same behavior.
     *
     *   days: N
     *      Remove inactive incident history older than N days.
     *
     *   no headers
     *      Remove all inactive incident history.
     *
     * Normal cleanup never removes active incidents. The previous_runs cleanup
     * is different: it removes everything before the most recent SERVER_START.
     */
    if(v1.getStringFromMap("X-PIOT-Incident-Cleanup", url.headers(), str)
       || v1.getStringFromMap("scope", url.headers(), str)) {

        if(str == "previous_runs") {

            const bool inactiveOnly = false;

            if(db->removeHistoryBeforeLastIncidentStart(inactiveOnly)) {
                makeStatusJSON(reply, STATUS_NO_CONTENT);
                (completion)(reply, STATUS_NO_CONTENT);
                return true;
            }

            makeStatusJSON(reply, STATUS_BAD_REQUEST,
                           "Delete Failed",
                           "Could not remove incident history from previous runs",
                           url.pathString());

            (completion)(reply, STATUS_BAD_REQUEST);
            return true;
        }

        makeStatusJSON(reply, STATUS_BAD_REQUEST,
                       "Invalid Incident Cleanup",
                       "X-PIOT-Incident-Cleanup must be previous_runs",
                       url.pathString());

        (completion)(reply, STATUS_BAD_REQUEST);
        return true;
    }

    float days = 0.0F;

    if(v1.getStringFromMap(JSON_HDR_DAYS, url.headers(), str)) {
        char* p = nullptr;
        days = strtof(str.c_str(), &p);

        if(p == str.c_str() || *p != 0 || days < 0.0F) {
            makeStatusJSON(reply, STATUS_BAD_REQUEST,
                           "Invalid Days",
                           "days must be a positive number or zero",
                           url.pathString());

            (completion)(reply, STATUS_BAD_REQUEST);
            return true;
        }
    }

    /*
     * Normal incident cleanup is always inactive-only from REST.
     * Active incidents should be cleared by incident recovery logic, not by
     * generic history cleanup.
     */
    const bool inactiveOnly = true;

    if(db->removeHistoryForIncidents(days, inactiveOnly)) {
        makeStatusJSON(reply, STATUS_NO_CONTENT);
        (completion)(reply, STATUS_NO_CONTENT);
        return true;
    }

    makeStatusJSON(reply, STATUS_BAD_REQUEST,
                   "Delete Failed",
                   "Could not remove inactive incident history",
                   url.pathString());

    (completion)(reply, STATUS_BAD_REQUEST);
    return true;
}

static void Incidents_NounHandler([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                  REST_URL url,
                                  [[maybe_unused]] TCPClientInfo cInfo,
                                  ServerCmdQueue::cmdCallback_t completion) {

    using namespace rest;

    json reply;
    bool isValidURL = false;

    switch(url.method()){
        case HTTP_GET:
            isValidURL = Incidents_NounHandler_GET(cmdQueue, url, cInfo, completion);
            break;

        case HTTP_PATCH:
            isValidURL = Incidents_NounHandler_PATCH(cmdQueue, url, cInfo, completion);
            break;

        case HTTP_DELETE:
            isValidURL = Incidents_NounHandler_DELETE(cmdQueue, url, cInfo, completion);
            break;

        default:
            (completion)(reply, STATUS_INVALID_METHOD);
            return;
    }

    if(!isValidURL) {
        (completion)(reply, STATUS_NOT_FOUND);
    }
}


// MARK: -  RULE NOUN HANDLERS



static bool Rule_NounHandler_GET([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                     REST_URL url,
                                     [[maybe_unused]] TCPClientInfo cInfo,
                                     ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;

    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();
    json reply;

    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

     vector<ruleID_t> rids;

     if(queries.size() == 0){
         rids = db->allruleIDs();
     }
     else {
         for (std::string const& key : std::views::keys(queries)){
             ruleID_t rid;
             if(str_to_RuleID(key.c_str(), &rid))
                 rids.push_back(rid);
         }
     }

    json allRules;

    for(auto rid : rids){
        json js = db->ruleJSON(rid);
        if(!js.is_null()){
            string ridStr = to_hex<unsigned short>(rid);
            allRules[ridStr] = js;
            }
    }


    reply[string(PROP_ARG_RULE_IDS)] = allRules;

    makeStatusJSON(reply, STATUS_OK);
    (completion)(reply, STATUS_OK);
    return false;
}


static bool Rule_NounHandler_DELETE([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                        REST_URL url,
                                        [[maybe_unused]] TCPClientInfo cInfo,
                                        ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();

    json reply;

    ServerCmdArgValidator v1;
    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    ruleID_t rid;

    if( !str_to_RuleID(path.at(1).c_str(), &rid) || !db->ruleIDIsValid(rid))
        return false;

    if(path.size() == 2) {
        if(db->ruleDelete(rid)){
            makeStatusJSON(reply,STATUS_NO_CONTENT);
            (completion) (reply, STATUS_NO_CONTENT);
        }
        else {
            reply[string(PROP_ARG_RULE_ID)] = RuleID_to_string(rid);
            makeStatusJSON(reply, STATUS_BAD_REQUEST, "Delete Failed" );;
            (completion) (reply, STATUS_BAD_REQUEST);
        }
        return true;

    }
    return false;
}


static bool Rule_NounHandler_POST([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                      REST_URL url,
                                      [[maybe_unused]] TCPClientInfo cInfo,
                                      ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();

    json reply;

    ServerCmdArgValidator v1;

    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();

    if(path.size() == 1) {
        // Create event

        ruleID_t rid;

        Rule rule = Rule(url.body());
        if(rule.isValid()
           && db->ruleSave(rule, &rid)) {
               reply[string(PROP_ARG_RULE_ID)] = RuleID_to_string(rid);
               reply[string(JSON_ARG_NAME)] = rule.name();
               makeStatusJSON(reply,STATUS_OK);
               (completion) (reply, STATUS_OK);
            return true;
        }

        makeStatusJSON(reply, STATUS_BAD_REQUEST, "Create Rule Failed" );;
        (completion) (reply, STATUS_BAD_REQUEST);
        return true;
    }

    return false;
}


static bool Rule_NounHandler_PATCH([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                       REST_URL url,
                                       [[maybe_unused]] TCPClientInfo cInfo,
                                       ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;

    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();

    json reply;

    ServerCmdArgValidator v1;
    auto pIoTServer = pIoTServerMgr::shared();
     auto db = pIoTServer->getDB();

       if(path.size() == 2) {
            ruleID_t rid;

            if( !str_to_RuleID(path.at(1).c_str(), &rid)
                || !db->ruleIDIsValid(rid))
                  return false;


           bool failed = false;
           bool success = false;

           // set name
           string newName;
           if(v1.getStringFromJSON(JSON_ARG_NAME, url.body(), newName)){
               if(db->ruleSetName(rid, newName)) {
                   reply[string(PROP_ARG_RULE_ID)] = RuleID_to_string(rid);
                   reply[string(JSON_ARG_NAME)] = newName;
                   success = true;
               }
               else {
                   failed = true;
               }
           }

           // set Description
           string newDescr;
           if(v1.getStringFromJSON(PROP_DESCRIPTION, url.body(), newDescr)){
               if(db->ruleSetDescription(rid, newDescr)) {
                   reply[string(PROP_ARG_RULE_ID)] = RuleID_to_string(rid);
                   reply[string(PROP_DESCRIPTION)] = newDescr;
                   success = true;
               }
               else {
                   failed = true;
               }
           }

           bool  enable = false;
           if(v1.getBoolFromJSON(JSON_ARG_ENABLE, url.body(), enable)){
               if(db->ruleSetEnable(rid, enable)) {
                    reply[string(PROP_ARG_RULE_ID)] = RuleID_to_string(rid);
                    reply[JSON_ARG_ENABLE] = enable;
                    success = true;
               }
               else {
                   failed = true;
               }
           }

           if(success) {
               makeStatusJSON(reply,STATUS_OK);
               (completion) (reply, STATUS_OK);
               return true;
           }

           if(failed){
               reply[string(PROP_ARG_RULE_ID)] = RuleID_to_string(rid);
               makeStatusJSON(reply, STATUS_BAD_REQUEST, "Update Failed" );;
               (completion) (reply, STATUS_BAD_REQUEST);
               return true;
           }

           reply[string(PROP_ARG_RULE_ID)] = RuleID_to_string(rid);
           makeStatusJSON(reply, STATUS_BAD_REQUEST,
                          "Body Invalid",
                          "Body missing argument",
                          url.pathString());
           (completion) (reply, STATUS_BAD_REQUEST);

           return true;
      }
    return false;
}


static bool Rule_NounHandler_PUT([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                     REST_URL url,
                                     [[maybe_unused]] TCPClientInfo cInfo,
                                     ServerCmdQueue::cmdCallback_t completion) {

        using namespace rest;
        auto path = url.path();
        auto queries = url.queries();
        auto headers = url.headers();

        json reply;

        ServerCmdArgValidator v1;
        auto pIoTServer = pIoTServerMgr::shared();
        auto db = pIoTServer->getDB();

        if(path.size() == 1) {

            ruleID_t rid;
            string str;

            if(v1.getStringFromJSON(PROP_ARG_RULE_ID, url.body(), str)){

                if( !str_to_RuleID(str.c_str(), &rid) || !db->ruleIDIsValid(rid))
                    return false;

                if(db->ruleIsActive(rid)){
                    reply[string(PROP_ARG_RULE_ID)] = RuleID_to_string(rid);
                    makeStatusJSON(reply, STATUS_CONFLICT, "Conflict", "Rule is already active.");
                    (completion) (reply, STATUS_CONFLICT);
                    return true;
                }

                bool queued = pIoTServer->triggerRule(rid);

                if(queued){
                    reply[string(PROP_ARG_RULE_ID)] = RuleID_to_string(rid);

                    makeStatusJSON(reply,STATUS_OK);
                    (completion) (reply, STATUS_OK);
                }
                else{
                    makeStatusJSON(reply, STATUS_BAD_REQUEST, "URL Invalid", "The value key provided was malformed or null");
                    (completion) (reply, STATUS_BAD_REQUEST);

                }
            }
        }



    makeStatusJSON(reply, STATUS_BAD_REQUEST,
                   "Body Invalid",
                   "Body missing argument",
                   url.pathString());
    (completion) (reply, STATUS_BAD_REQUEST);

    return true;

}


static void Rule_NounHandler([[maybe_unused]] ServerCmdQueue* cmdQueue,
                                  REST_URL url,
                                  [[maybe_unused]] TCPClientInfo cInfo,
                                  ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    json reply;

    auto path = url.path();
    auto queries = url.queries();
    auto headers = url.headers();
    string noun;

    bool isValidURL = false;

    if(path.size() > 0) {
        noun = path.at(0);
    }

    switch(url.method()){
        case HTTP_GET:
            isValidURL = Rule_NounHandler_GET(cmdQueue,url,cInfo, completion);
            break;

        case HTTP_PUT:
            isValidURL = Rule_NounHandler_PUT(cmdQueue,url,cInfo, completion);
            break;

        case HTTP_PATCH:
            isValidURL = Rule_NounHandler_PATCH(cmdQueue,url,cInfo, completion);
            break;

        case HTTP_POST:
            isValidURL = Rule_NounHandler_POST(cmdQueue,url,cInfo, completion);
            break;

        case HTTP_DELETE:
            isValidURL = Rule_NounHandler_DELETE(cmdQueue,url,cInfo, completion);
            break;

        default:
            (completion) (reply, STATUS_INVALID_METHOD);
            return;
    }

    if(!isValidURL) {
        (completion) (reply, STATUS_NOT_FOUND);
    }

}


// MARK: -  register server nouns

void registerServerNouns() {
    // create the server command processor
    auto cmdQueue = ServerCmdQueue::shared();

    cmdQueue->registerNoun(NOUN_VERSION,    Version_NounHandler);
    cmdQueue->registerNoun(NOUN_DATE,       Date_NounHandler);
    cmdQueue->registerNoun(NOUN_LOG,        Log_NounHandler);

    cmdQueue->registerNoun(NOUN_SCHEMA,     Schema_NounHandler);

    cmdQueue->registerNoun(NOUN_STATE,      State_NounHandler);
    cmdQueue->registerNoun(NOUN_VALUES,     Values_NounHandler);
    cmdQueue->registerNoun(NOUN_RANGE,     ValuesRange_NounHandler);

    cmdQueue->registerNoun(NOUN_PROPERTIES, Properties_NounHandler);
    cmdQueue->registerNoun(NOUN_HISTORY,    History_NounHandler);

    cmdQueue->registerNoun(NOUN_DEVICES,    Device_NounHandler);

    cmdQueue->registerNoun(NOUN_SEQUENCES,      Sequences_NounHandler);
    cmdQueue->registerNoun(NOUN_SEQUENCE_GROUPS,  SequenceGroups_NounHandler);

    cmdQueue->registerNoun(NOUN_INCIDENTS,  Incidents_NounHandler);

    cmdQueue->registerNoun(NOUN_PING,      Ping_NounHandler);

    cmdQueue->registerNoun(NOUN_RULES,  Rule_NounHandler);

    cmdQueue->registerNoun(NOUN_TEST,  Test_NounHandler);


}
