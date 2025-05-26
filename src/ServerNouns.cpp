//
//  ServerNouns.cpp
//  demoServer
//
//  Created by Vincent Moscaritolo on 2/10/22.
//

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <functional>
#include <vector>
#include <string>
#include <exception>
#include <filesystem>

#include <stdexcept>
#include <cstring>            //Needed for memset and string functions
#include <type_traits>
#include <map>
#include <ranges>

#include "ServerNouns.hpp"
#include "ServerCmdQueue.hpp"
#include "RESTServerConnection.hpp"
#include "ServerCmdQueue.hpp"

#include "ServerCmdValidators.hpp"
#include "pIoTServerMgr.hpp"
#include "TimeStamp.hpp"
#include "PropValKeys.hpp"
#include "LogMgr.hpp"
#include "Utils.hpp"
 
#include "I2C.hpp"
#include "W1_Device.hpp"

#include <sys/utsname.h>

// MARK: -  SCHEMA NOUN HANDLERS

static void Schema_NounHandler(ServerCmdQueue* cmdQueue,
                               REST_URL url,
                               TCPClientInfo cInfo,
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

static bool History_NounHandler_GET(ServerCmdQueue* cmdQueue,
                                    REST_URL url,
                                    TCPClientInfo cInfo,
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
        
        if(schema.tracking == TR_TRACK_CHANGES) {
            
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
            
            
            if(schema.tracking == TR_TRACK_CHANGES) {
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

static bool History_NounHandler_DELETE(ServerCmdQueue* cmdQueue,
                                       REST_URL url,
                                       TCPClientInfo cInfo,
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
        if(schema.tracking == TR_TRACK_CHANGES) {
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

static void History_NounHandler(ServerCmdQueue* cmdQueue,
                                REST_URL url,
                                TCPClientInfo cInfo,
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

// MARK: -  ALERTS NOUN HANDLER

static bool Alerts_NounHandler_GET(ServerCmdQueue* cmdQueue,
                                   REST_URL url,
                                   TCPClientInfo cInfo,
                                   ServerCmdQueue::cmdCallback_t completion) {
    using namespace rest;
    json reply;
    ServerCmdArgValidator v1;
    auto path = url.path();
    
    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();
    
    size_t pathsize = path.size();
    
    if(pathsize == 1){
        pIoTServerDB::historicAlerts_t alerts;
        string str;
        
        float days = 0;
        int limit = 0;
        int offset = 0;
        
        if(v1.getStringFromMap(JSON_HDR_LIMIT, url.headers(), str)){
            char* p;
            limit = (int) strtol(str.c_str(), &p, 10);
            //           if(*p != 0) days = 0;
        }
        
        if(v1.getStringFromMap(JSON_HDR_DAYS, url.headers(), str)){
            char* p;
            days =  strtof(str.c_str(), &p);
            //         if(*p != 0) days = 0;
        }
        if(v1.getStringFromMap(JSON_HDR_OFFSET, url.headers(), str)){
            char* p;
            offset = (int) strtol(str.c_str(), &p, 10);
            //       if(*p != 0) days = 0;
        }
        
        if(db->historyForAlerts(alerts, days, limit, offset)){
            
            json j;
            for (auto &entry : alerts) {
                json j1;
                
                alert_t alertID = get<1>(entry);
                string details = get<2>(entry);
                
                j1[JSON_ARG_TIME]           = get<0>(entry);
                j1[JSON_ARG_ALERT_STRING]   = pIoTServerDB::displayStringForAlert(alertID);
                if(details.size())
                    j1[JSON_ARG_ALERT_DETAILS]  = details;
                
                j.push_back(j1);
            }
            
            reply[string(JSON_ARG_ALERT)] = j;
        }
    }
    else if(pathsize == 2){
        string subpath =  path.at(1);
        
        if( subpath == SUBPATH_COUNT){
            
            int count;
            
            if(db->countHistoryForAlerts(count)){
                reply[JSON_ARG_COUNT] = count;
            }
        }
    }
    else
    {
        makeStatusJSON(reply, STATUS_BAD_REQUEST,
                       "URL Invalid",
                       "The request takes no arguments",
                       url.pathString());
        
        (completion) (reply, STATUS_BAD_REQUEST);
        return true;
    }
    
    makeStatusJSON(reply,STATUS_OK);
    (completion) (reply, STATUS_OK);
    return true;
}

static bool Alerts_NounHandler_DELETE(ServerCmdQueue* cmdQueue,
                                      REST_URL url,
                                      TCPClientInfo cInfo,
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
    float days = 0;
    
    if(v1.getStringFromMap(JSON_HDR_DAYS, url.headers(), str)){
        char* p;
        days =  strtof(str.c_str(), &p);
        if(*p != 0) days = 0;
    }
    
    if(db->removehistoryForAlerts(days)){
        makeStatusJSON(reply,STATUS_NO_CONTENT);
        (completion) (reply, STATUS_NO_CONTENT);
        return true;
    }
    
    return false;
}

static void Alerts_NounHandler(ServerCmdQueue* cmdQueue,
                               REST_URL url,
                               TCPClientInfo cInfo,
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
            isValidURL = Alerts_NounHandler_GET(cmdQueue,url,cInfo, completion);
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
            isValidURL = Alerts_NounHandler_DELETE(cmdQueue,url,cInfo, completion);
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

static bool Properties_NounHandler_GET(ServerCmdQueue* cmdQueue,
                                       REST_URL url,
                                       TCPClientInfo cInfo,
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
    
    json propEntries;
    auto filter = {PROP_CONFIG} ;
    
    if(path.size() == 1){
        db->getAllProperties(filter ,&propEntries);
    }else if (path.size() == 2){
        // CHECK sub paths
        string propName = path.at(1);
        json prop;
        
        auto found = std::find(filter.begin(), filter.end(), propName);
        if (found == filter.end() &&
            db->getJSONProperty(propName, &prop)){
            propEntries[propName] = prop;
        }
        else {
            return false;
        }
    }
    else {
        return false;
    }
    
    reply[ string(JSON_ARG_PROPERTIES) ] = propEntries;
    
    makeStatusJSON(reply,STATUS_OK);
    (completion) (reply, STATUS_OK);
    return true;
}


static bool Properties_NounHandler_PUT(ServerCmdQueue* cmdQueue,
                                       REST_URL url,
                                       TCPClientInfo cInfo,
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


static bool Properties_NounHandler_DELETE(ServerCmdQueue* cmdQueue,
                                          REST_URL url,
                                          TCPClientInfo cInfo,
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




static void Properties_NounHandler(ServerCmdQueue* cmdQueue,
                                   REST_URL url,
                                   TCPClientInfo cInfo,
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

static bool Log_NounHandler_PATCH(ServerCmdQueue* cmdQueue,
                                  REST_URL url,
                                  TCPClientInfo cInfo,
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

static void Log_NounHandler(ServerCmdQueue* cmdQueue,
                            REST_URL url,  // entire request
                            TCPClientInfo cInfo,
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

static bool State_NounHandler_GET(ServerCmdQueue* cmdQueue,
                                  REST_URL url,
                                  TCPClientInfo cInfo,
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


static bool State_NounHandler_PUT(ServerCmdQueue* cmdQueue,
                                  REST_URL url,
                                  TCPClientInfo cInfo,
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


static void State_NounHandler(ServerCmdQueue* cmdQueue,
                              REST_URL url,
                              TCPClientInfo cInfo,
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

static bool Values_NounHandler_GET(ServerCmdQueue* cmdQueue,
                                   REST_URL url,
                                   TCPClientInfo cInfo,
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

static bool Values_NounHandler_PUT(ServerCmdQueue* cmdQueue,
                                   REST_URL url,
                                   TCPClientInfo cInfo,
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

static void Values_NounHandler(ServerCmdQueue* cmdQueue,
                               REST_URL url,  // entire request
                               TCPClientInfo cInfo,
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

static bool ValuesRange_NounHandler_GET(ServerCmdQueue* cmdQueue,
                                   REST_URL url,
                                   TCPClientInfo cInfo,
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



static void ValuesRange_NounHandler(ServerCmdQueue* cmdQueue,
                               REST_URL url,  // entire request
                               TCPClientInfo cInfo,
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

static void Date_NounHandler(ServerCmdQueue* cmdQueue,
                             REST_URL url,
                             TCPClientInfo cInfo,
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
      
        sj[JSON_ARG_SOLAR_POM_VISABLE] = solar.moonVisable;
        sj[JSON_ARG_SOLAR_POM_STR] = solar.moonPhaseName;
        sj[JSON_ARG_SOLAR_POM_PHASE] = solar.moonPhase;
        reply[JSON_ARG_SOLAR] = sj;
    }
    
    makeStatusJSON(reply,STATUS_OK);
    (completion) (reply, STATUS_OK);
}

// MARK: - version - NOUN HANDLER

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


static void Version_NounHandler(ServerCmdQueue* cmdQueue,
                                REST_URL url,
                                TCPClientInfo cInfo,
                                ServerCmdQueue::cmdCallback_t completion) {
    
    using namespace rest;
    using namespace timestamp;
    auto pIoTServer = pIoTServerMgr::shared();
    auto db = pIoTServer->getDB();
    
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
    
    reply[string(JSON_ARG_UPTIME)]    = pIoTServer->upTime();
    reply[string(JSON_ARG_VERSION)]     = pIoTServerMgr::pIoTServerMgr_Version;
    reply[string(JSON_ARG_BUILD_TIME)]    =  string(__DATE__) + " " + string(__TIME__);
    
    string instanceName;
    if(db->getConfigProperty(string(JSON_ARG_NAME), instanceName)){
        reply[string(JSON_ARG_NAME)] = instanceName;
    }
    
    string description;
    if(db->getConfigProperty(string(PROP_DESCRIPTION), description)){
        reply[string(PROP_DESCRIPTION)] = description;
    }
    
    std::string procname;
    std::ifstream("/proc/self/comm") >> procname;
    if(procname.size())
        reply[string(JSON_ARG_SERVER_PROCNAME)] = string(procname);
    
    
    if(std::filesystem::exists(".dockerenv")) {
        reply[string("DOCKER")] = true;
    }
    
    map<string,string> info = {};
    if(getCPUinfo(info) && info.size() > 0){
        for(auto &[key,val] : info){
             reply["cpu."+key] = val;
        }
    }
   
    struct utsname buffer;
    if (uname(&buffer) == 0){
        reply[string(JSON_ARG_OS_SYSNAME)] =   string(buffer.sysname);
        reply[string(JSON_ARG_OS_NODENAME)] =   string(buffer.nodename);
        reply[string(JSON_ARG_OS_RELEASE)] =   string(buffer.release);
        reply[string(JSON_ARG_OS_VERSION)] =   string(buffer.version);
        reply[string(JSON_ARG_OS_MACHINE)] =   string(buffer.machine);
    }
    
    makeStatusJSON(reply,STATUS_OK);
    (completion) (reply, STATUS_OK);
}



// MARK: -  DEVICE NOUN HANDLERS



static bool Device_NounHandler_GET(ServerCmdQueue* cmdQueue,
                                   REST_URL url,
                                   TCPClientInfo cInfo,
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
            
            // askimg for all devices also gives you the I2c and W1 maps
            stringvector hexAddr;
            std::vector<uint8_t>  addrs;
            if(I2C::getI2CAddressMap(addrs))
            {
                for(auto hex :addrs){
                    hexAddr.push_back(to_hex(hex));
                }
            }
            reply[JSON_ARG_I2C] = hexAddr;
            
            std::vector<std::string> w1Devices;
            W1_Device::getW1Devices(w1Devices);
            reply[JSON_ARG_W1DEVICE] = w1Devices;
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


static bool Device_NounHandler_PUT(ServerCmdQueue* cmdQueue,
                                   REST_URL url,
                                   TCPClientInfo cInfo,
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
static void Device_NounHandler(ServerCmdQueue* cmdQueue,
                               REST_URL url,  // entire request
                               TCPClientInfo cInfo,
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



static bool Sequence_NounHandler_GET(ServerCmdQueue* cmdQueue,
                                     REST_URL url,
                                     TCPClientInfo cInfo,
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
  
    if(queries.size() == 0){
        sids = db->allSequenceIDs();
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
 
    for(auto sid : sids){
        json js = db->sequenceJSON(sid);
        if(!js.is_null()){
            string sidStr = to_hex<unsigned short>(sid);
            allSequences[sidStr] = js;
            
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
 
    reply[string(JSON_ARG_SEQUENCE_IDS)] = allSequences;
    reply[string(JSON_ARG_CRON_SEQUENCES)] = cronSequences;
    reply[string(JSON_ARG_FUTURE_SEQUENCES)] = futureSequences;
    reply[string(JSON_ARG_TIMED_SEQUENCES)] = timedSequences;
   
    makeStatusJSON(reply,STATUS_OK);
    (completion) (reply, STATUS_OK);
    return false;
}


static bool Sequence_NounHandler_DELETE(ServerCmdQueue* cmdQueue,
                                        REST_URL url,
                                        TCPClientInfo cInfo,
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


static bool Sequence_NounHandler_POST(ServerCmdQueue* cmdQueue,
                                      REST_URL url,
                                      TCPClientInfo cInfo,
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


static bool Sequence_NounHandler_PATCH(ServerCmdQueue* cmdQueue,
                                       REST_URL url,
                                       TCPClientInfo cInfo,
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


static bool Sequence_NounHandler_PUT(ServerCmdQueue* cmdQueue,
                                     REST_URL url,
                                     TCPClientInfo cInfo,
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

static void Sequences_NounHandler(ServerCmdQueue* cmdQueue,
                                  REST_URL url,
                                  TCPClientInfo cInfo,
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

static bool SequenceGroup_NounHandler_NounHandler_GET(ServerCmdQueue* cmdQueue,
                                                      REST_URL url,
                                                      TCPClientInfo cInfo,
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

static bool SequenceGroup_NounHandler_POST(ServerCmdQueue* cmdQueue,
                                           REST_URL url,
                                           TCPClientInfo cInfo,
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


static bool SequenceGroup_NounHandler_DELETE(ServerCmdQueue* cmdQueue,
                                             REST_URL url,
                                             TCPClientInfo cInfo,
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

static bool SequenceGroup_NounHandler_PATCH(ServerCmdQueue* cmdQueue,
                                            REST_URL url,
                                            TCPClientInfo cInfo,
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

static bool SequenceGroup_NounHandler_PUT(ServerCmdQueue* cmdQueue,
                                          REST_URL url,
                                          TCPClientInfo cInfo,
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

static void SequenceGroups_NounHandler(ServerCmdQueue* cmdQueue,
                                       REST_URL url,
                                       TCPClientInfo cInfo,
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

static void Test_NounHandler(ServerCmdQueue* cmdQueue,
                             REST_URL url,
                             TCPClientInfo cInfo,
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
    
    cmdQueue->registerNoun(NOUN_ALERTS,     Alerts_NounHandler);
    
    cmdQueue->registerNoun(NOUN_DEVICES,    Device_NounHandler);
    
    cmdQueue->registerNoun(NOUN_SEQUENCES,      Sequences_NounHandler);
    cmdQueue->registerNoun(NOUN_SEQUENCE_GROUPS,  SequenceGroups_NounHandler);
    
    
    
    cmdQueue->registerNoun(NOUN_TEST,  Test_NounHandler);
    
    
}

