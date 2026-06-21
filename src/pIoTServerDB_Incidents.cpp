//
//  pIoTServerDB_Incidents.cpp
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


// MARK: -   SQL DATABASE INCIDENTS

bool pIoTServerDB::initIncidentTables(){

    std::lock_guard<std::mutex> lock(_mutex);

    if(!_sdb) {
        return false;
    }

    char *zErrMsg = 0;

    string sql1 = "CREATE TABLE IF NOT EXISTS INCIDENT("
    "ID             INTEGER PRIMARY KEY AUTOINCREMENT,"
    "SOURCE         TEXT       NOT NULL,"
    "CODE           TEXT       NOT NULL,"
    "INCIDENT_KEY   TEXT       NOT NULL,"
    "SEVERITY       INTEGER    NOT NULL,"
    "ACTIVE         INTEGER    NOT NULL DEFAULT 0,"
    "FIRST_DATE     NUMERIC    NOT NULL,"
    "LAST_DATE      NUMERIC    NOT NULL,"
    "CLEARED_DATE   NUMERIC    DEFAULT 0,"
    "COUNT          INTEGER    NOT NULL DEFAULT 1,"
    "ETAG           INTEGER    NOT NULL,"
    "MESSAGE        TEXT,"
    "DETAILS        TEXT"
    ");";

    if(sqlite3_exec(_sdb, sql1.c_str(), NULL, 0, &zErrMsg) != SQLITE_OK){
        LOGT_ERROR("sqlite3_exec FAILED: %s\n\t%s", sql1.c_str(), sqlite3_errmsg(_sdb));
        sqlite3_free(zErrMsg);
        return false;
    }

    string sql2 = "CREATE TABLE IF NOT EXISTS INCIDENT_ETAG("
    "NAME           TEXT       PRIMARY KEY NOT NULL,"
    "ETAG           INTEGER    NOT NULL"
    ");";

    if(sqlite3_exec(_sdb, sql2.c_str(), NULL, 0, &zErrMsg) != SQLITE_OK){
        LOGT_ERROR("sqlite3_exec FAILED: %s\n\t%s", sql2.c_str(), sqlite3_errmsg(_sdb));
        sqlite3_free(zErrMsg);
        return false;
    }

    string sql3 = "INSERT OR IGNORE INTO INCIDENT_ETAG(NAME, ETAG) VALUES('INCIDENT', 0);";

    if(sqlite3_exec(_sdb, sql3.c_str(), NULL, 0, &zErrMsg) != SQLITE_OK){
        LOGT_ERROR("sqlite3_exec FAILED: %s\n\t%s", sql3.c_str(), sqlite3_errmsg(_sdb));
        sqlite3_free(zErrMsg);
        return false;
    }

    string sql4 = "CREATE INDEX IF NOT EXISTS IDX_INCIDENT_ACTIVE "
    "ON INCIDENT(ACTIVE, SOURCE, CODE, INCIDENT_KEY);";

    if(sqlite3_exec(_sdb, sql4.c_str(), NULL, 0, &zErrMsg) != SQLITE_OK){
        LOGT_ERROR("sqlite3_exec FAILED: %s\n\t%s", sql4.c_str(), sqlite3_errmsg(_sdb));
        sqlite3_free(zErrMsg);
        return false;
    }

    string sql5 = "CREATE INDEX IF NOT EXISTS IDX_INCIDENT_ETAG "
    "ON INCIDENT(ETAG);";

    if(sqlite3_exec(_sdb, sql5.c_str(), NULL, 0, &zErrMsg) != SQLITE_OK){
        LOGT_ERROR("sqlite3_exec FAILED: %s\n\t%s", sql5.c_str(), sqlite3_errmsg(_sdb));
        sqlite3_free(zErrMsg);
        return false;
    }

    string sql6 = "CREATE UNIQUE INDEX IF NOT EXISTS IDX_INCIDENT_ONE_ACTIVE "
    "ON INCIDENT(SOURCE, CODE, INCIDENT_KEY) WHERE ACTIVE = 1;";

    if(sqlite3_exec(_sdb, sql6.c_str(), NULL, 0, &zErrMsg) != SQLITE_OK){
        LOGT_ERROR("sqlite3_exec FAILED: %s\n\t%s", sql6.c_str(), sqlite3_errmsg(_sdb));
        sqlite3_free(zErrMsg);
        return false;
    }

    return true;
}


bool pIoTServerDB::nextIncidentEtagLocked(int64_t &etagOut){

    etagOut = 0;

    if(!_sdb) {
        return false;
    }

    sqlite3_stmt* stmt = NULL;

    try {
        string sql = "UPDATE INCIDENT_ETAG SET ETAG = ETAG + 1 WHERE NAME = 'INCIDENT';";

        if(sqlite3_prepare_v2(_sdb, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
            throw std::runtime_error("prepare UPDATE INCIDENT_ETAG failed");

        if(sqlite3_step(stmt) != SQLITE_DONE)
            throw std::runtime_error("step UPDATE INCIDENT_ETAG failed");

        sqlite3_finalize(stmt); stmt = NULL;

        sql = "SELECT ETAG FROM INCIDENT_ETAG WHERE NAME = 'INCIDENT';";

        if(sqlite3_prepare_v2(_sdb, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
            throw std::runtime_error("prepare SELECT INCIDENT_ETAG failed");

        if(sqlite3_step(stmt) != SQLITE_ROW)
            throw std::runtime_error("step SELECT INCIDENT_ETAG failed");

        etagOut = sqlite3_column_int64(stmt, 0);

        sqlite3_finalize(stmt); stmt = NULL;

        return etagOut > 0;

    } catch (const std::runtime_error& e) {
        LOGT_ERROR("nextIncidentEtagLocked FAILED: %s", e.what());
    }

    if(stmt) sqlite3_finalize(stmt);

    return false;
}

bool pIoTServerDB::noticeIncident(const std::string& source,
                                  const std::string& code,
                                  const std::string& key,
                                  const char* message,
                                  const char* details){

    std::lock_guard<std::mutex> lock(_mutex);

    if(!_sdb || source.empty() || code.empty() || key.empty()) {
        return false;
    }

    bool success = false;
    sqlite3_stmt* stmt = NULL;

    try {
        if(sqlite3_exec(_sdb, "BEGIN;", NULL, NULL, NULL) != SQLITE_OK)
            throw std::runtime_error("BEGIN failed");

        int64_t etag = 0;
        if(!nextIncidentEtagLocked(etag))
            throw std::runtime_error("nextIncidentEtagLocked failed");

        time_t now = time(NULL);

        string sql = "INSERT INTO INCIDENT("
        "SOURCE, CODE, INCIDENT_KEY, SEVERITY, ACTIVE, "
        "FIRST_DATE, LAST_DATE, CLEARED_DATE, COUNT, ETAG, MESSAGE, DETAILS"
        ") VALUES(?,?,?,?,?,?,?,?,?,?,?,?);";

        if(sqlite3_prepare_v2(_sdb, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
            throw std::runtime_error("prepare INSERT INCIDENT failed");

        sqlite3_bind_text(stmt,   1, source.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt,   2, code.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt,   3, key.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt,    4, 1);
        sqlite3_bind_int(stmt,    5, 0);
        sqlite3_bind_int64(stmt,  6, now);
        sqlite3_bind_int64(stmt,  7, now);
        sqlite3_bind_int64(stmt,  8, 0);
        sqlite3_bind_int(stmt,    9, 1);
        sqlite3_bind_int64(stmt, 10, etag);

        if(message)
            sqlite3_bind_text(stmt, 11, message, -1, SQLITE_STATIC);
        else
            sqlite3_bind_null(stmt, 11);

        if(details)
            sqlite3_bind_text(stmt, 12, details, -1, SQLITE_STATIC);
        else
            sqlite3_bind_null(stmt, 12);

        if(sqlite3_step(stmt) != SQLITE_DONE)
            throw std::runtime_error("step INSERT INCIDENT failed");

        sqlite3_finalize(stmt); stmt = NULL;

        if(sqlite3_exec(_sdb, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)
            throw std::runtime_error("COMMIT failed");

        success = true;

    } catch (const std::runtime_error& e) {
        LOGT_ERROR("noticeIncident FAILED: %s", e.what());
        sqlite3_exec(_sdb, "ROLLBACK;", NULL, NULL, NULL);
    }

    if(stmt) sqlite3_finalize(stmt);

    return success;
}

bool pIoTServerDB::raiseIncident(const std::string& source,
                                 int severity,
                                 const std::string& code,
                                 const std::string& key,
                                 const char* message,
                                 const char* details){

    std::lock_guard<std::mutex> lock(_mutex);

    if(!_sdb || source.empty() || code.empty() || key.empty()) {
        return false;
    }

    bool success = false;
    sqlite3_stmt* stmt = NULL;

    try {
        if(sqlite3_exec(_sdb, "BEGIN;", NULL, NULL, NULL) != SQLITE_OK)
            throw std::runtime_error("BEGIN failed");

        int64_t etag = 0;
        if(!nextIncidentEtagLocked(etag))
            throw std::runtime_error("nextIncidentEtagLocked failed");

        time_t now = time(NULL);

        string sql = "UPDATE INCIDENT SET "
        "SEVERITY = ?, "
        "LAST_DATE = ?, "
        "COUNT = COUNT + 1, "
        "ETAG = ? ";

        if(message) {
            sql += ", MESSAGE = ? ";
        }

        if(details) {
            sql += ", DETAILS = ? ";
        }

        sql += "WHERE SOURCE = ? AND CODE = ? AND INCIDENT_KEY = ? AND ACTIVE = 1;";

        if(sqlite3_prepare_v2(_sdb, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
            throw std::runtime_error("prepare UPDATE INCIDENT failed");

        int bind = 1;

        sqlite3_bind_int(stmt, bind++, severity);
        sqlite3_bind_int64(stmt, bind++, now);
        sqlite3_bind_int64(stmt, bind++, etag);

        if(message) {
            sqlite3_bind_text(stmt, bind++, message, -1, SQLITE_STATIC);
        }

        if(details) {
            sqlite3_bind_text(stmt, bind++, details, -1, SQLITE_STATIC);
        }

        sqlite3_bind_text(stmt, bind++, source.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, bind++, code.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, bind++, key.c_str(), -1, SQLITE_STATIC);

        if(sqlite3_step(stmt) != SQLITE_DONE)
            throw std::runtime_error("step UPDATE INCIDENT failed");

        int changed = sqlite3_changes(_sdb);

        sqlite3_finalize(stmt); stmt = NULL;

        if(changed == 0) {
            sql = "INSERT INTO INCIDENT("
            "SOURCE, CODE, INCIDENT_KEY, SEVERITY, ACTIVE, "
            "FIRST_DATE, LAST_DATE, CLEARED_DATE, COUNT, ETAG, MESSAGE, DETAILS"
            ") VALUES(?,?,?,?,?,?,?,?,?,?,?,?);";

            if(sqlite3_prepare_v2(_sdb, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
                throw std::runtime_error("prepare INSERT INCIDENT failed");

            sqlite3_bind_text(stmt,   1, source.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt,   2, code.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt,   3, key.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_int(stmt,    4, severity);
            sqlite3_bind_int(stmt,    5, 1);
            sqlite3_bind_int64(stmt,  6, now);
            sqlite3_bind_int64(stmt,  7, now);
            sqlite3_bind_int64(stmt,  8, 0);
            sqlite3_bind_int(stmt,    9, 1);
            sqlite3_bind_int64(stmt, 10, etag);

            if(message)
                sqlite3_bind_text(stmt, 11, message, -1, SQLITE_STATIC);
            else
                sqlite3_bind_null(stmt, 11);

            if(details)
                sqlite3_bind_text(stmt, 12, details, -1, SQLITE_STATIC);
            else
                sqlite3_bind_null(stmt, 12);

            if(sqlite3_step(stmt) != SQLITE_DONE)
                throw std::runtime_error("step INSERT INCIDENT failed");

            sqlite3_finalize(stmt); stmt = NULL;
        }

        if(sqlite3_exec(_sdb, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)
            throw std::runtime_error("COMMIT failed");

        success = true;

    } catch (const std::runtime_error& e) {
        LOGT_ERROR("raiseIncident FAILED: %s", e.what());
        sqlite3_exec(_sdb, "ROLLBACK;", NULL, NULL, NULL);
    }

    if(stmt) sqlite3_finalize(stmt);

    return success;
}

bool pIoTServerDB::clearIncident(const std::string& source,
                                 const std::string& code,
                                 const std::string& key,
                                 const char* message,
                                 const char* details){

    std::lock_guard<std::mutex> lock(_mutex);

    if(!_sdb || source.empty() || code.empty() || key.empty()) {
        return false;
    }

    bool success = false;
    sqlite3_stmt* stmt = NULL;

    try {
        string findSql = "SELECT ID FROM INCIDENT "
        "WHERE SOURCE = ? AND CODE = ? AND INCIDENT_KEY = ? AND ACTIVE = 1 "
        "LIMIT 1;";

        if(sqlite3_prepare_v2(_sdb, findSql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
            throw std::runtime_error("prepare SELECT active INCIDENT failed");

        sqlite3_bind_text(stmt, 1, source.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, code.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, key.c_str(), -1, SQLITE_STATIC);

        bool hasActiveIncident = sqlite3_step(stmt) == SQLITE_ROW;

        sqlite3_finalize(stmt); stmt = NULL;

        if(!hasActiveIncident) {
            return true;
        }

        if(sqlite3_exec(_sdb, "BEGIN;", NULL, NULL, NULL) != SQLITE_OK)
            throw std::runtime_error("BEGIN failed");

        int64_t etag = 0;
        if(!nextIncidentEtagLocked(etag))
            throw std::runtime_error("nextIncidentEtagLocked failed");

        time_t now = time(NULL);

        string sql = "UPDATE INCIDENT SET "
        "ACTIVE = 0, "
        "LAST_DATE = ?, "
        "CLEARED_DATE = ?, "
        "ETAG = ? ";

        if(message) {
            sql += ", MESSAGE = ? ";
        }

        if(details) {
            sql += ", DETAILS = ? ";
        }

        sql += "WHERE SOURCE = ? AND CODE = ? AND INCIDENT_KEY = ? AND ACTIVE = 1;";

        if(sqlite3_prepare_v2(_sdb, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK)
            throw std::runtime_error("prepare UPDATE clear INCIDENT failed");

        int bind = 1;

        sqlite3_bind_int64(stmt, bind++, now);
        sqlite3_bind_int64(stmt, bind++, now);
        sqlite3_bind_int64(stmt, bind++, etag);

        if(message) {
            sqlite3_bind_text(stmt, bind++, message, -1, SQLITE_STATIC);
        }

        if(details) {
            sqlite3_bind_text(stmt, bind++, details, -1, SQLITE_STATIC);
        }

        sqlite3_bind_text(stmt, bind++, source.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, bind++, code.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, bind++, key.c_str(), -1, SQLITE_STATIC);

        if(sqlite3_step(stmt) != SQLITE_DONE)
            throw std::runtime_error("step UPDATE clear INCIDENT failed");

        sqlite3_finalize(stmt); stmt = NULL;

        if(sqlite3_exec(_sdb, "COMMIT;", NULL, NULL, NULL) != SQLITE_OK)
            throw std::runtime_error("COMMIT failed");

        success = true;

    } catch (const std::runtime_error& e) {
        LOGT_ERROR("clearIncident FAILED: %s", e.what());
        sqlite3_exec(_sdb, "ROLLBACK;", NULL, NULL, NULL);
    }

    if(stmt) sqlite3_finalize(stmt);

    return success;
}

bool pIoTServerDB::incidentGetEtag(int64_t &etagOut){

    std::lock_guard<std::mutex> lock(_mutex);

    bool success = false;
    etagOut = 0;

    if(!_sdb) {
        return false;
    }

    sqlite3_stmt* stmt = NULL;
    string sql = "SELECT ETAG FROM INCIDENT_ETAG WHERE NAME = 'INCIDENT';";

    if(sqlite3_prepare_v2(_sdb, sql.c_str(), -1, &stmt, NULL) == SQLITE_OK){

        if(sqlite3_step(stmt) == SQLITE_ROW) {
            etagOut = sqlite3_column_int64(stmt, 0);
            success = true;
        }

        sqlite3_finalize(stmt);
    }
    else {
        LOGT_ERROR("sqlite3_prepare_v2 FAILED: %s\n\t%s", sql.c_str(), sqlite3_errmsg(_sdb));
    }

    return success;
}

bool pIoTServerDB::historyForIncidents(historicIncidents_t &incidentsOut,
                                       float days,
                                       int limit,
                                       int offset,
                                       bool activeOnly,
                                       int64_t sinceEtag){

    std::lock_guard<std::mutex> lock(_mutex);

    bool success = false;
    historicIncidents_t incidents;
    incidents.clear();

    if(!_sdb) {
        return false;
    }

    sqlite3_stmt* stmt = NULL;

    string sql = "SELECT ID, SOURCE, CODE, INCIDENT_KEY, SEVERITY, ACTIVE, "
    "FIRST_DATE, LAST_DATE, CLEARED_DATE, COUNT, ETAG, MESSAGE, DETAILS "
    "FROM INCIDENT ";

    bool hasWhere = false;

    if(activeOnly) {
        sql += "WHERE ACTIVE = 1 ";
        hasWhere = true;
    }

    if(sinceEtag > 0) {
        sql += hasWhere ? "AND " : "WHERE ";
        sql += "ETAG > ? ";
        hasWhere = true;
    }

    if(days > 0) {
        sql += hasWhere ? "AND " : "WHERE ";
        sql += "datetime(LAST_DATE, 'auto') > datetime('now', '-" + to_string(days) + " days', 'localtime') ";
        hasWhere = true;
    }

    sql += "ORDER BY ETAG DESC ";

    if(limit > 0) {
        sql += "LIMIT " + to_string(limit) + " ";

        if(offset > 0) {
            sql += "OFFSET " + to_string(offset) + " ";
        }
    }

    sql += ";";

    if(sqlite3_prepare_v2(_sdb, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK) {
        LOGT_ERROR("sqlite3_prepare_v2 FAILED: %s\n\t%s", sql.c_str(), sqlite3_errmsg(_sdb));
        return false;
    }

    int bind = 1;

    if(sinceEtag > 0) {
        sqlite3_bind_int64(stmt, bind++, sinceEtag);
    }

    while(sqlite3_step(stmt) == SQLITE_ROW) {

        int64_t id          = sqlite3_column_int64(stmt, 0);
        const char* source  = (const char*)sqlite3_column_text(stmt, 1);
        const char* code    = (const char*)sqlite3_column_text(stmt, 2);
        const char* key     = (const char*)sqlite3_column_text(stmt, 3);
        int severity        = sqlite3_column_int(stmt, 4);
        bool active         = sqlite3_column_int(stmt, 5) != 0;
        time_t firstDate    = sqlite3_column_int64(stmt, 6);
        time_t lastDate     = sqlite3_column_int64(stmt, 7);
        time_t clearedDate  = sqlite3_column_int64(stmt, 8);
        int count           = sqlite3_column_int(stmt, 9);
        int64_t etag        = sqlite3_column_int64(stmt, 10);
        const char* message = (const char*)sqlite3_column_text(stmt, 11);
        const char* details = (const char*)sqlite3_column_text(stmt, 12);

        incidents.push_back(std::make_tuple(
            id,
            source ? string(source) : string(),
            code ? string(code) : string(),
            key ? string(key) : string(),
            severity,
            active,
            firstDate,
            lastDate,
            clearedDate,
            count,
            etag,
            message ? string(message) : string(),
            details ? string(details) : string()
        ));
    }

    sqlite3_finalize(stmt);

    incidentsOut = incidents;
    success = true;

    return success;
}

bool pIoTServerDB::countHistoryForIncidents(int &countOut,
                                            bool activeOnly){

    std::lock_guard<std::mutex> lock(_mutex);

    bool success = false;
    int count = 0;

    if(!_sdb) {
        return false;
    }

    sqlite3_stmt* stmt = NULL;

    string sql = "SELECT COUNT(*) FROM INCIDENT ";

    if(activeOnly) {
        sql += "WHERE ACTIVE = 1 ";
    }

    sql += ";";

    if(sqlite3_prepare_v2(_sdb, sql.c_str(), -1, &stmt, NULL) == SQLITE_OK){

        if(sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
            success = true;
        }

        sqlite3_finalize(stmt);
    }
    else {
        LOGT_ERROR("sqlite3_prepare_v2 FAILED: %s\n\t%s", sql.c_str(), sqlite3_errmsg(_sdb));
    }

    if(success) {
        countOut = count;
    }

    return success;
}

bool pIoTServerDB::removeHistoryForIncidents(float days,
                                             bool inactiveOnly) {

    std::lock_guard<std::mutex> lock(_mutex);

    bool success = false;

    if(!_sdb) {
        return false;
    }

    sqlite3_stmt* stmt = NULL;

    string sql = "DELETE FROM INCIDENT ";

    bool hasWhere = false;

    if(inactiveOnly) {
        sql += "WHERE ACTIVE = 0 ";
        hasWhere = true;
    }

    if(days > 0) {
        sql += hasWhere ? "AND " : "WHERE ";
        sql += "datetime(LAST_DATE, 'auto') < datetime('now', '-" + to_string(days) + " days', 'localtime') ";
        hasWhere = true;
    }

    sql += ";";

    if(sqlite3_prepare_v2(_sdb, sql.c_str(), -1, &stmt, NULL) == SQLITE_OK) {

        if(sqlite3_step(stmt) == SQLITE_DONE) {
            int count = sqlite3_changes(_sdb);
            LOGT_DEBUG("sqlite %s\n %d rows affected", sql.c_str(), count);
            success = true;
        }
        else {
            LOGT_ERROR("sqlite3_step FAILED: %s\n\t%s", sql.c_str(), sqlite3_errmsg(_sdb));
        }

        sqlite3_finalize(stmt);
    }
    else {
        LOGT_ERROR("sqlite3_prepare_v2 FAILED: %s\n\t%s", sql.c_str(), sqlite3_errmsg(_sdb));
    }

    return success;
}

bool pIoTServerDB::removeHistoryBeforeLastIncidentStart(bool inactiveOnly) {

    std::lock_guard<std::mutex> lock(_mutex);

    bool success = false;

    if(!_sdb) {
        return false;
    }

    sqlite3_stmt* stmt = NULL;

    string sql = "DELETE FROM INCIDENT ";
    sql += "WHERE datetime(LAST_DATE, 'auto') < (";
    sql += "SELECT datetime(LAST_DATE, 'auto') ";
    sql += "FROM INCIDENT ";
    sql += "WHERE CODE = 'SERVER_START' ";
    sql += "ORDER BY datetime(LAST_DATE, 'auto') DESC, ID DESC ";
    sql += "LIMIT 1";
    sql += ") ";

    if(inactiveOnly) {
        sql += "AND ACTIVE = 0 ";
    }

    sql += ";";

    if(sqlite3_prepare_v2(_sdb, sql.c_str(), -1, &stmt, NULL) == SQLITE_OK) {

        if(sqlite3_step(stmt) == SQLITE_DONE) {
            int count = sqlite3_changes(_sdb);
            LOGT_DEBUG("sqlite %s\n %d rows affected", sql.c_str(), count);
            success = true;
        }
        else {
            LOGT_ERROR("sqlite3_step FAILED: %s\n\t%s", sql.c_str(), sqlite3_errmsg(_sdb));
        }

        sqlite3_finalize(stmt);
    }
    else {
        LOGT_ERROR("sqlite3_prepare_v2 FAILED: %s\n\t%s", sql.c_str(), sqlite3_errmsg(_sdb));
    }

    return success;
}
