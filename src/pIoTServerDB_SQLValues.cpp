//
//  pIoTServerDB_SQLValues.cpp
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

// MARK: -  SQL DATABASE VALUES

bool pIoTServerDB::initLogDatabase(string assetPath){

    if(_sdb) {
        sqlite3_close(_sdb);
        _sdb = NULL;
    }

    string dbFileName = "piotserver.db";

    if(assetPath.size())
        dbFileName = assetPath + "/" + dbFileName;

    LOGT_DEBUG("OPEN database: %s", dbFileName.c_str());

    if(sqlite3_open(dbFileName.c_str(), &_sdb) != SQLITE_OK){
        LOGT_ERROR("sqlite3_open FAILED: %s", dbFileName.c_str(), sqlite3_errmsg(_sdb    ) );
        return false;
    }

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

    if(!initIncidentTables()){
         LOG_ERROR("initIncidentTables FAILED\n");
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

        _values[key] = make_pair(when, value) ;
        _etagMap[key] = nextEtag();
    }
    sqlite3_finalize(stmt);


    return statusOk;
}



bool pIoTServerDB::insertValueToDB(string key, string value, time_t time ){

    std::lock_guard<std::mutex> lock(_mutex);
    bool success = false;

    string sql = string("INSERT INTO DEVICE_DATA (NAME,DATE,VALUE) VALUES (?,?,?);");

    sqlite3_stmt* stmt = NULL;
    sqlite3_prepare_v2(_sdb, sql.c_str(), -1,  &stmt, NULL);
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_double(stmt, 2,  time);
    sqlite3_bind_text(stmt, 3, value.c_str(), -1, SQLITE_STATIC);

    if(sqlite3_step(stmt) == SQLITE_DONE) {
        success = true;
    }
    else
    {
        LOGT_ERROR("sqlite3_step FAILED: %s\n\t%s", sql.c_str(), sqlite3_errmsg(_sdb    ) );
    }
    sqlite3_finalize(stmt);
    return success;
}


bool pIoTServerDB::insertRangeToDB(string key, double minVal, double maxVal, time_t time ){

    std::lock_guard<std::mutex> lock(_mutex);
    bool success = false;

    sqlite3_stmt* stmt = NULL;

    try {
        if( sqlite3_prepare_v2(_sdb, "INSERT INTO DEVICE_RANGE (NAME,DATE,MIN,MAX)  VALUES (?,?,?,?)", -1,  &stmt, NULL)  != SQLITE_OK)
            throw std::runtime_error("prepare INSERT failed ");

        if( sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC)   != SQLITE_OK)
            throw std::runtime_error("bind key failed ");

        if( sqlite3_bind_double(stmt, 2,  time) != SQLITE_OK)
            throw std::runtime_error("bind time failed ");

        if( sqlite3_bind_double(stmt, 3,  minVal)   != SQLITE_OK)
            throw std::runtime_error("bind Min failed ");

        if( sqlite3_bind_double(stmt, 4,  maxVal)   != SQLITE_OK)
            throw std::runtime_error("bind max failed ");

        if( sqlite3_step(stmt)  != SQLITE_DONE)
            throw std::runtime_error("step failed ");

        sqlite3_finalize(stmt); stmt = NULL;

        success = true;

    } catch (const std::runtime_error& e) {

        std::cerr << "Caught runtime error: " << e.what() << std::endl;
    }

    if(stmt) sqlite3_finalize(stmt);

    return success;
}



bool pIoTServerDB::saveUniqueValueToDB(string key, string value, time_t time ){

    std::lock_guard<std::mutex> lock(_mutex);
    bool success = false;

    sqlite3_stmt* stmt = NULL;

    try {
        if( sqlite3_exec(_sdb, "BEGIN;", NULL, NULL, NULL) != SQLITE_OK)
            throw std::runtime_error("BEGIN statement failed ");

        if( sqlite3_prepare_v2(_sdb, "DELETE FROM DEVICE_DATA WHERE NAME = ?;", -1,  &stmt, NULL)  != SQLITE_OK)
            throw std::runtime_error("prepare DELETE failed ");

        if( sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC)  != SQLITE_OK)
            throw std::runtime_error("bind failed ");

        if( sqlite3_step(stmt)  != SQLITE_DONE)
            throw std::runtime_error("step failed ");

        sqlite3_finalize(stmt); stmt = NULL;

        if( sqlite3_prepare_v2(_sdb, "INSERT INTO DEVICE_DATA (NAME,DATE,VALUE) VALUES(?,?,?);", -1,  &stmt, NULL)  != SQLITE_OK)
            throw std::runtime_error("prepare INSERT failed ");

        if( sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC)  != SQLITE_OK)
            throw std::runtime_error("bind key failed ");

        if( sqlite3_bind_double(stmt, 2, time)  != SQLITE_OK)
            throw std::runtime_error("bind time failed ");

        if( sqlite3_bind_text(stmt, 3, value.c_str(), -1, SQLITE_STATIC)  != SQLITE_OK)
            throw std::runtime_error("bind value failed ");

        if( sqlite3_step(stmt)  != SQLITE_DONE)
            throw std::runtime_error("step failed ");

        sqlite3_finalize(stmt); stmt = NULL;

        if( sqlite3_exec(_sdb, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)
            throw std::runtime_error("COMMIT statement failed ");

        success = true;

    } catch (const std::runtime_error& e) {

        std::cerr << "Caught runtime error: " << e.what() << std::endl;
    }

    if(stmt) sqlite3_finalize(stmt);

    return success;
}

bool pIoTServerDB::getMinMaxForValues(stringvector keys, double, vector<minMaxEntry_t> &entries){
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

    sqlite3_stmt* stmt = NULL;
    string sql;

    try {
        sql = string("DELETE FROM DEVICE_DATA ");

        if(!key.empty()) {
            sql += "WHERE NAME = ? ";
        }

        if(days > 0) {

            if(key.size() > 0) {
                sql += "AND ";
            }
            else {
                sql += "WHERE ";
            }
            sql += "datetime(DATE, 'auto') > datetime('now' , '-" + to_string(days) + " days', 'localtime')";
        }

        sql += ";";

        if( sqlite3_prepare_v2(_sdb, sql.c_str(), -1,  &stmt, NULL)  != SQLITE_OK)
            throw std::runtime_error("prepare INSERT failed ");

        if(!key.empty()) {
            if( sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC)   != SQLITE_OK)
                throw std::runtime_error("bind key failed ");
        }

        if( sqlite3_step(stmt)  != SQLITE_DONE)
            throw std::runtime_error("step failed ");

        int count =  sqlite3_changes(_sdb);
        LOGT_DEBUG("removeHistoryForKey: %d rows affected", count );

        sqlite3_finalize(stmt); stmt = NULL;

        success = true;

    } catch (const std::runtime_error& e) {

        std::cerr << "Caught runtime error: " << e.what() << std::endl;
    }

    if(stmt) sqlite3_finalize(stmt);
    return success;

}

bool pIoTServerDB::countHistoryForKey(string key, int &countOut){
    std::lock_guard<std::mutex> lock(_mutex);
    bool success = false;
    int count = 0;
    sqlite3_stmt* stmt = NULL;

    try {
        if( sqlite3_prepare_v2(_sdb, "SELECT COUNT(*) FROM DEVICE_DATA WHERE NAME = ?;", -1,  &stmt, NULL)  != SQLITE_OK)
            throw std::runtime_error("prepare INSERT failed ");

        if( sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC)   != SQLITE_OK)
            throw std::runtime_error("bind key failed ");

        if( sqlite3_step(stmt)  != SQLITE_ROW)
            throw std::runtime_error("step failed ");

        count = sqlite3_column_int(stmt, 0);

        sqlite3_finalize(stmt); stmt = NULL;
        success = true;

    } catch (const std::runtime_error& e) {
        std::cerr << "Caught runtime error: " << e.what() << std::endl;
    }

    if(success){
        countOut = count;
    }

    return success;
}

bool pIoTServerDB::historyForKey(string key, historicValues_t &valuesOut,
                                 int days, int limit, int offset){

    std::lock_guard<std::mutex> lock(_mutex);
    bool success = false;

    sqlite3_stmt* stmt = NULL;
    string sql;

    historicValues_t values;
    values.clear();

    try {
        sql = string("SELECT DATE, VALUE FROM DEVICE_DATA WHERE NAME = ? ");

        if(limit){
            sql += " ORDER BY DATE DESC LIMIT " + to_string(limit);

            if(offset)
                sql += " OFFSET " + to_string(offset);
        }
        else if(days > 0) {
            sql += " AND datetime(DATE, 'auto') > datetime('now' , '-" + to_string(days) + " days', 'localtime')";
        }
        sql += ";" ;

        if( sqlite3_prepare_v2(_sdb, sql.c_str(), -1,  &stmt, NULL)  != SQLITE_OK)
            throw std::runtime_error("prepare INSERT failed ");

        if( sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC)   != SQLITE_OK)
            throw std::runtime_error("bind key failed ");

        while ( (sqlite3_step(stmt)) == SQLITE_ROW) {
            time_t  when =  sqlite3_column_int64(stmt, 0);
            string  value = string((char*) sqlite3_column_text(stmt, 1));
            values.push_back(make_pair(when, value)) ;
        }

        sqlite3_finalize(stmt); stmt = NULL;

        success = values.size() > 0;

    } catch (const std::runtime_error& e) {

        std::cerr << "Caught runtime error: " << e.what() << std::endl;
    }

    if(success){
        valuesOut = values;
    }

    if(stmt) sqlite3_finalize(stmt);
    return success;
}

bool pIoTServerDB::removeHistoryForRange(string key, float days){

    std::lock_guard<std::mutex> lock(_mutex);
    bool success = false;

    sqlite3_stmt* stmt = NULL;
    string sql;

    try {
        sql = string("DELETE FROM DEVICE_RANGE ");

        if(!key.empty()) {
            sql += "WHERE NAME = ? ";
        }

        if(days > 0) {

            if(key.size() > 0) {
                sql += "AND ";
            }
            else {
                sql += "WHERE ";
            }
            sql += "datetime(DATE, 'auto') > datetime('now' , '-" + to_string(days) + " days', 'localtime')";
        }

        sql += ";";

        if( sqlite3_prepare_v2(_sdb, sql.c_str(), -1,  &stmt, NULL)  != SQLITE_OK)
            throw std::runtime_error("prepare INSERT failed ");

        if(!key.empty()) {
            if( sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC)   != SQLITE_OK)
                throw std::runtime_error("bind key failed ");
        }

        if( sqlite3_step(stmt)  != SQLITE_DONE)
            throw std::runtime_error("step failed ");

        int count =  sqlite3_changes(_sdb);
        LOGT_DEBUG("removeHistoryForKey: %d rows affected", count );

        sqlite3_finalize(stmt); stmt = NULL;

        success = true;

    } catch (const std::runtime_error& e) {

        std::cerr << "Caught runtime error: " << e.what() << std::endl;
    }

    if(stmt) sqlite3_finalize(stmt);
    return success;
}

bool pIoTServerDB::countHistoryForRange(string key, int &countOut){

    std::lock_guard<std::mutex> lock(_mutex);
    bool success = false;
    int count = 0;
    sqlite3_stmt* stmt = NULL;

    try {
        if( sqlite3_prepare_v2(_sdb, "SELECT COUNT(*) FROM DEVICE_RANGE WHERE NAME = ?;", -1,  &stmt, NULL)  != SQLITE_OK)
            throw std::runtime_error("prepare INSERT failed ");

        if( sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC)   != SQLITE_OK)
            throw std::runtime_error("bind key failed ");

        if( sqlite3_step(stmt)  != SQLITE_ROW)
            throw std::runtime_error("step failed ");

        count = sqlite3_column_int(stmt, 0);

        sqlite3_finalize(stmt); stmt = NULL;
        success = true;

    } catch (const std::runtime_error& e) {
        std::cerr << "Caught runtime error: " << e.what() << std::endl;
    }

    if(success){
        countOut = count;
    }

    return success;
}

bool pIoTServerDB::historyForRange(string key, historicRanges_t &historyOut,
                                   int days, int limit, int offset){

    std::lock_guard<std::mutex> lock(_mutex);
    bool success = false;

    sqlite3_stmt* stmt = NULL;
    string sql;

    historicRanges_t ranges;
    ranges.clear();

    try {
        sql = string("SELECT DATE, MIN, MAX FROM DEVICE_RANGE WHERE NAME = ? ");

        if(limit){
            sql += " ORDER BY DATE DESC LIMIT " + to_string(limit);

            if(offset)
                sql += " OFFSET " + to_string(offset);
        }
        else if(days > 0) {
            sql += " AND datetime(DATE, 'auto') > datetime('now' , '-" + to_string(days) + " days', 'localtime')";
        }
        sql += ";" ;

        if( sqlite3_prepare_v2(_sdb, sql.c_str(), -1,  &stmt, NULL)  != SQLITE_OK)
            throw std::runtime_error("prepare INSERT failed ");

        if( sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC)   != SQLITE_OK)
            throw std::runtime_error("bind key failed ");

        while ( (sqlite3_step(stmt)) == SQLITE_ROW) {
            time_t  when =  sqlite3_column_int64(stmt, 0);
            double  min =   sqlite3_column_double(stmt, 1);
            double  max =   sqlite3_column_double(stmt, 2);
            ranges.push_back( make_tuple(when, min, max) ) ;
       }

        sqlite3_finalize(stmt); stmt = NULL;

        success = ranges.size() > 0;

    } catch (const std::runtime_error& e) {

        std::cerr << "Caught runtime error: " << e.what() << std::endl;
    }

    if(success){
        historyOut = ranges;
    }

    if(stmt) sqlite3_finalize(stmt);
    return success;
}
