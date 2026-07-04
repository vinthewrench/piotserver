//
//  pIoTServerDB_Tracking.cpp
//  pIoTServer
//
//  Duration tracking history support.
//  Stores completed active sessions only:
//
//      VALUE_NAME
//      START_TIME
//      DURATION_SEC
//      ETAG
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


// MARK: - Tracking History


bool pIoTServerDB::initTrackingTables()
{
    if(!_sdb) {
        return false;
    }

    const char* createTrackingSQL =
        "CREATE TABLE IF NOT EXISTS TRACKING ("
        "ID INTEGER PRIMARY KEY AUTOINCREMENT,"
        "VALUE_NAME TEXT NOT NULL,"
        "START_TIME INTEGER NOT NULL,"
        "DURATION_SEC INTEGER NOT NULL,"
        "ETAG INTEGER NOT NULL DEFAULT 0"
        ");";

    char* errMsg = nullptr;

    if(sqlite3_exec(_sdb, createTrackingSQL, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOGT_ERROR("initTrackingTables create TRACKING failed: %s",
                   errMsg ? errMsg : sqlite3_errmsg(_sdb));

        if(errMsg) {
            sqlite3_free(errMsg);
        }

        return false;
    }

    const char* createValueTimeIndexSQL =
        "CREATE INDEX IF NOT EXISTS TRACKING_VALUE_TIME_IDX "
        "ON TRACKING(VALUE_NAME, START_TIME);";

    if(sqlite3_exec(_sdb, createValueTimeIndexSQL, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOGT_ERROR("initTrackingTables create TRACKING_VALUE_TIME_IDX failed: %s",
                   errMsg ? errMsg : sqlite3_errmsg(_sdb));

        if(errMsg) {
            sqlite3_free(errMsg);
        }

        return false;
    }

    const char* createEtagIndexSQL =
        "CREATE INDEX IF NOT EXISTS TRACKING_ETAG_IDX "
        "ON TRACKING(ETAG);";

    if(sqlite3_exec(_sdb, createEtagIndexSQL, nullptr, nullptr, &errMsg) != SQLITE_OK) {
        LOGT_ERROR("initTrackingTables create TRACKING_ETAG_IDX failed: %s",
                   errMsg ? errMsg : sqlite3_errmsg(_sdb));

        if(errMsg) {
            sqlite3_free(errMsg);
        }

        return false;
    }

    return true;
}

bool pIoTServerDB::insertTrackingDuration(std::string valueName,
                                          time_t startTime,
                                          uint32_t durationSec) {
    bool success = false;

    if (!_sdb) {
        return false;
    }

    if (valueName.empty()) {
        return false;
    }

    if (startTime <= 0) {
        return false;
    }

    const char* sql =
        "INSERT INTO TRACKING "
        "(VALUE_NAME, START_TIME, DURATION_SEC, ETAG) "
        "VALUES (?, ?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(_sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOGT_ERROR("insertTrackingDuration prepare failed: %s", sqlite3_errmsg(_sdb));
        return false;
    }

    eTag_t tag = _eTag;

    sqlite3_bind_text(stmt, 1, valueName.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(startTime));
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(durationSec));
    sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(tag));

    if (sqlite3_step(stmt) == SQLITE_DONE) {
        success = true;
        nextEtag();
    }
    else {
        LOGT_ERROR("insertTrackingDuration step failed: %s", sqlite3_errmsg(_sdb));
    }

    sqlite3_finalize(stmt);

    return success;
}


bool pIoTServerDB::historyForTracking(std::string valueName,
                                      float days,
                                      int limit,
                                      int offset,
                                      int64_t sinceEtag,
                                      trackingHistory_t* trackingOut) {
    bool success = false;

    if (!_sdb) {
        return false;
    }

    if (trackingOut) {
        trackingOut->clear();
    }

    std::stringstream sql;

    sql << "SELECT ID, VALUE_NAME, START_TIME, DURATION_SEC, ETAG "
        << "FROM TRACKING WHERE 1=1 ";

    if (!valueName.empty()) {
        sql << "AND VALUE_NAME = ? ";
    }

    if (days > 0) {
        sql << "AND START_TIME >= ? ";
    }

    if (sinceEtag > 0) {
        sql << "AND ETAG > ? ";
    }

    sql << "ORDER BY START_TIME DESC, ID DESC ";

    if (limit > 0) {
        sql << "LIMIT ? ";
    }

    if (offset > 0) {
        sql << "OFFSET ? ";
    }

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(_sdb, sql.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        LOGT_ERROR("historyForTracking prepare failed: %s", sqlite3_errmsg(_sdb));
        return false;
    }

    int bindIndex = 1;

    if (!valueName.empty()) {
        sqlite3_bind_text(stmt, bindIndex++, valueName.c_str(), -1, SQLITE_TRANSIENT);
    }

    if (days > 0) {
        time_t cutoff = time(nullptr) - static_cast<time_t>(days * 86400.0f);
        sqlite3_bind_int64(stmt, bindIndex++, static_cast<sqlite3_int64>(cutoff));
    }

    if (sinceEtag > 0) {
        sqlite3_bind_int64(stmt, bindIndex++, static_cast<sqlite3_int64>(sinceEtag));
    }

    if (limit > 0) {
        sqlite3_bind_int(stmt, bindIndex++, limit);
    }

    if (offset > 0) {
        sqlite3_bind_int(stmt, bindIndex++, offset);
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        trackingEntry_t entry = {};

        entry.trackingID  = sqlite3_column_int64(stmt, 0);

        const unsigned char* txt = sqlite3_column_text(stmt, 1);
        if (txt) {
            entry.valueName = reinterpret_cast<const char*>(txt);
        }

        entry.startTime   = static_cast<time_t>(sqlite3_column_int64(stmt, 2));
        entry.durationSec = static_cast<uint32_t>(sqlite3_column_int64(stmt, 3));
        entry.eTag        = static_cast<eTag_t>(sqlite3_column_int64(stmt, 4));

        if (trackingOut) {
            trackingOut->push_back(entry);
        }
    }

    success = true;

    sqlite3_finalize(stmt);

    return success;
}


bool pIoTServerDB::countHistoryForTracking(std::string valueName,
                                           float days,
                                           int64_t sinceEtag,
                                           int* countOut) {
    bool success = false;

    if (!_sdb) {
        return false;
    }

    if (countOut) {
        *countOut = 0;
    }

    std::stringstream sql;

    sql << "SELECT COUNT(*) FROM TRACKING WHERE 1=1 ";

    if (!valueName.empty()) {
        sql << "AND VALUE_NAME = ? ";
    }

    if (days > 0) {
        sql << "AND START_TIME >= ? ";
    }

    if (sinceEtag > 0) {
        sql << "AND ETAG > ? ";
    }

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(_sdb, sql.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        LOGT_ERROR("countHistoryForTracking prepare failed: %s", sqlite3_errmsg(_sdb));
        return false;
    }

    int bindIndex = 1;

    if (!valueName.empty()) {
        sqlite3_bind_text(stmt, bindIndex++, valueName.c_str(), -1, SQLITE_TRANSIENT);
    }

    if (days > 0) {
        time_t cutoff = time(nullptr) - static_cast<time_t>(days * 86400.0f);
        sqlite3_bind_int64(stmt, bindIndex++, static_cast<sqlite3_int64>(cutoff));
    }

    if (sinceEtag > 0) {
        sqlite3_bind_int64(stmt, bindIndex++, static_cast<sqlite3_int64>(sinceEtag));
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        if (countOut) {
            *countOut = sqlite3_column_int(stmt, 0);
        }

        success = true;
    }
    else {
        LOGT_ERROR("countHistoryForTracking step failed: %s", sqlite3_errmsg(_sdb));
    }

    sqlite3_finalize(stmt);

    return success;
}


bool pIoTServerDB::removeHistoryForTracking(std::string valueName,
                                            float days) {
    bool success = false;

    if (!_sdb) {
        return false;
    }

    std::stringstream sql;

    sql << "DELETE FROM TRACKING WHERE 1=1 ";

    if (!valueName.empty()) {
        sql << "AND VALUE_NAME = ? ";
    }

    if (days > 0) {
        sql << "AND START_TIME < ? ";
    }

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(_sdb, sql.str().c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        LOGT_ERROR("removeHistoryForTracking prepare failed: %s", sqlite3_errmsg(_sdb));
        return false;
    }

    int bindIndex = 1;

    if (!valueName.empty()) {
        sqlite3_bind_text(stmt, bindIndex++, valueName.c_str(), -1, SQLITE_TRANSIENT);
    }

    if (days > 0) {
        time_t cutoff = time(nullptr) - static_cast<time_t>(days * 86400.0f);
        sqlite3_bind_int64(stmt, bindIndex++, static_cast<sqlite3_int64>(cutoff));
    }

    if (sqlite3_step(stmt) == SQLITE_DONE) {
        success = true;
    }
    else {
        LOGT_ERROR("removeHistoryForTracking step failed: %s", sqlite3_errmsg(_sdb));
    }

    sqlite3_finalize(stmt);

    return success;
}


bool pIoTServerDB::removeAllTracking() {
    bool success = false;

    if (!_sdb) {
        return false;
    }

    const char* sql = "DELETE FROM TRACKING;";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(_sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOGT_ERROR("removeAllTracking prepare failed: %s", sqlite3_errmsg(_sdb));
        return false;
    }

    if (sqlite3_step(stmt) == SQLITE_DONE) {
        success = true;
    }
    else {
        LOGT_ERROR("removeAllTracking step failed: %s", sqlite3_errmsg(_sdb));
    }

    sqlite3_finalize(stmt);

    return success;
}

bool pIoTServerDB::maxETagForTracking(eTag_t* eTagOut) {
    bool success = false;

    if (!_sdb) {
        return false;
    }

    if (eTagOut) {
        *eTagOut = 0;
    }

    /*
     * TRACKING has an index on ETAG, so MAX(ETAG) is a cheap way to let the
     * REST/API layer decide whether it needs to run the heavier summary query.
     */
    const char* sql =
        "SELECT COALESCE(MAX(ETAG), 0) "
        "FROM TRACKING;";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(_sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOGT_ERROR("maxETagForTracking prepare failed: %s", sqlite3_errmsg(_sdb));
        return false;
    }

    int rc = sqlite3_step(stmt);

    if (rc == SQLITE_ROW) {
        if (eTagOut) {
            *eTagOut = static_cast<eTag_t>(sqlite3_column_int64(stmt, 0));
        }

        success = true;
    }
    else {
        LOGT_ERROR("maxETagForTracking step failed: %s", sqlite3_errmsg(_sdb));
    }

    sqlite3_finalize(stmt);

    return success;
}


bool pIoTServerDB::todayStartForTracking(time_t* todayStartOut) {
    bool success = false;

    if (!_sdb) {
        return false;
    }

    if (todayStartOut) {
        *todayStartOut = 0;
    }

    /*
     * "Today" in the tracking summary is based on local farm time.
     *
     * This value must travel with cached summary responses because today's
     * count/duration can change at local midnight even if TRACKING.ETAG has
     * not changed.
     *
     * strftime('%s', ...) returns text in SQLite, so cast it to INTEGER here.
     */
    const char* sql =
        "SELECT CAST(strftime('%s', 'now', 'localtime', 'start of day') AS INTEGER);";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(_sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOGT_ERROR("todayStartForTracking prepare failed: %s", sqlite3_errmsg(_sdb));
        return false;
    }

    int rc = sqlite3_step(stmt);

    if (rc == SQLITE_ROW) {
        if (todayStartOut) {
            *todayStartOut = static_cast<time_t>(sqlite3_column_int64(stmt, 0));
        }

        success = true;
    }
    else {
        LOGT_ERROR("todayStartForTracking step failed: %s", sqlite3_errmsg(_sdb));
    }

    sqlite3_finalize(stmt);

    return success;
}


bool pIoTServerDB::summaryForTracking(trackingSummary_t* summaryOut) {
    bool success = false;

    if (!_sdb) {
        return false;
    }

    if (summaryOut) {
        summaryOut->clear();
    }

    /*
     * Compact tracking summary.
     *
     * This returns one row per VALUE_NAME with:
     *
     *   - today's count and duration, using local midnight
     *   - lifetime count and duration
     *   - latest tracking row for that VALUE_NAME
     *
     * The latest row is selected by MAX(ID). ID is AUTOINCREMENT, and tracking
     * rows are append-only completed sessions, so this is the simplest stable
     * way to identify the most recently inserted row for each VALUE_NAME.
     *
     * The DB only stores VALUE_NAME. The REST/API layer should decorate the
     * value with deviceID/title/units from schema/config when needed.
     *
     * strftime('%s', ...) returns text in SQLite, so today_start casts it to
     * INTEGER before comparing it against START_TIME.
     */
    const char* sql =
        "WITH today_start(ts) AS ("
        "  SELECT CAST(strftime('%s', 'now', 'localtime', 'start of day') AS INTEGER)"
        "),"
        "totals AS ("
        "  SELECT "
        "    VALUE_NAME, "
        "    COUNT(*) AS total_count, "
        "    COALESCE(SUM(DURATION_SEC), 0) AS total_duration "
        "  FROM TRACKING "
        "  GROUP BY VALUE_NAME"
        "),"
        "today AS ("
        "  SELECT "
        "    VALUE_NAME, "
        "    COUNT(*) AS today_count, "
        "    COALESCE(SUM(DURATION_SEC), 0) AS today_duration "
        "  FROM TRACKING, today_start "
        "  WHERE START_TIME >= today_start.ts "
        "  GROUP BY VALUE_NAME"
        "),"
        "last AS ("
        "  SELECT "
        "    VALUE_NAME, "
        "    MAX(ID) AS last_id "
        "  FROM TRACKING "
        "  GROUP BY VALUE_NAME"
        ") "
        "SELECT "
        "  totals.VALUE_NAME, "
        "  COALESCE(today.today_count, 0) AS today_count, "
        "  COALESCE(today.today_duration, 0) AS today_duration, "
        "  totals.total_count, "
        "  totals.total_duration, "
        "  TRACKING.START_TIME AS last_time, "
        "  TRACKING.DURATION_SEC AS last_duration, "
        "  TRACKING.ID AS last_tracking_id, "
        "  TRACKING.ETAG AS last_etag "
        "FROM totals "
        "LEFT JOIN today ON today.VALUE_NAME = totals.VALUE_NAME "
        "LEFT JOIN last ON last.VALUE_NAME = totals.VALUE_NAME "
        "LEFT JOIN TRACKING ON TRACKING.ID = last.last_id "
        "ORDER BY totals.VALUE_NAME;";

    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(_sdb, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOGT_ERROR("summaryForTracking prepare failed: %s", sqlite3_errmsg(_sdb));
        return false;
    }

    int rc = SQLITE_OK;

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        trackingSummaryEntry_t entry = {};

        const unsigned char* txt = sqlite3_column_text(stmt, 0);
        if (txt) {
            entry.valueName = reinterpret_cast<const char*>(txt);
        }

        entry.todayCount       = sqlite3_column_int(stmt, 1);
        entry.todayDurationSec = sqlite3_column_int64(stmt, 2);
        entry.totalCount       = sqlite3_column_int(stmt, 3);
        entry.totalDurationSec = sqlite3_column_int64(stmt, 4);
        entry.lastStartTime    = static_cast<time_t>(sqlite3_column_int64(stmt, 5));
        entry.lastDurationSec  = static_cast<uint32_t>(sqlite3_column_int64(stmt, 6));
        entry.lastTrackingID   = sqlite3_column_int64(stmt, 7);
        entry.lastETag         = static_cast<eTag_t>(sqlite3_column_int64(stmt, 8));

        if (summaryOut) {
            summaryOut->push_back(entry);
        }
    }

    if (rc == SQLITE_DONE) {
        success = true;
    }
    else {
        LOGT_ERROR("summaryForTracking step failed: %s", sqlite3_errmsg(_sdb));
    }

    sqlite3_finalize(stmt);

    return success;
}
