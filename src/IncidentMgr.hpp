//
// IncidentMgr.hpp
//

#pragma once

#include <string>
#include <mutex>

class pIoTServerDB;

class IncidentMgr {
public:
    enum class Severity {
        Info     = 0,
        Notice   = 1,
        Warning  = 2,
        Error    = 3,
        Critical = 4
    };

    static IncidentMgr* shared();

    bool begin(pIoTServerDB* db);
    void stop();

    bool isSetup() const;

    bool notice(const std::string& source,
                const std::string& code,
                const std::string& key,
                const char* message = nullptr,
                const char* details = nullptr);

    bool raise(const std::string& source,
               Severity severity,
               const std::string& code,
               const std::string& key,
               const char* message = nullptr,
               const char* details = nullptr);

    bool clear(const std::string& source,
               const std::string& code,
               const std::string& key,
               const char* message = nullptr,
               const char* details = nullptr);

    bool notify(const std::string& title,
                const std::string& message,
                Severity severity);

private:
    IncidentMgr();

    static IncidentMgr* _sharedInstance;

    mutable std::mutex _mutex;
    pIoTServerDB* _db = nullptr;
    bool _isSetup = false;
};
