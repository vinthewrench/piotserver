//
// IncidentMgr.cpp
//

#include "IncidentMgr.hpp"
#include "pIoTServerDB.hpp"
#include "LogMgr.hpp"

IncidentMgr* IncidentMgr::_sharedInstance = nullptr;

IncidentMgr* IncidentMgr::shared()
{
    if(!_sharedInstance) {
        _sharedInstance = new IncidentMgr;
    }

    return _sharedInstance;
}

IncidentMgr::IncidentMgr()
{
}

bool IncidentMgr::begin(pIoTServerDB* db)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if(!db) {
        LOG_ERROR("IncidentMgr begin failed: db is null");
        return false;
    }

    _db = db;

    if(!_db->initIncidentTables()) {
        LOG_ERROR("IncidentMgr begin failed: initIncidentTables failed");
        _db = nullptr;
        _isSetup = false;
        return false;
    }

    _isSetup = true;
    return true;
}

void IncidentMgr::stop()
{
    std::lock_guard<std::mutex> lock(_mutex);

    _db = nullptr;
    _isSetup = false;
}

bool IncidentMgr::isSetup() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _isSetup;
}

bool IncidentMgr::notice(const std::string& source,
                         const std::string& code,
                         const std::string& key,
                         const char* message,
                         const char* details)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if(!_isSetup || !_db) {
        return false;
    }

    return _db->noticeIncident(source,
                               code,
                               key,
                               message,
                               details);
}

bool IncidentMgr::raise(const std::string& source,
                        Severity severity,
                        const std::string& code,
                        const std::string& key,
                        const char* message,
                        const char* details)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if(!_isSetup || !_db) {
        return false;
    }

    return _db->raiseIncident(source,
                              static_cast<int>(severity),
                              code,
                              key,
                              message,
                              details);
}

bool IncidentMgr::clear(const std::string& source,
                        const std::string& code,
                        const std::string& key,
                        const char* message,
                        const char* details)
{
    std::lock_guard<std::mutex> lock(_mutex);

    if(!_isSetup || !_db) {
        return false;
    }

    return _db->clearIncident(source,
                              code,
                              key,
                              message,
                              details);
}
