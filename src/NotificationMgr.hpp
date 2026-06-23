//
//  NotificationMgr.hpp
//  pIoTServer
//

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include "IncidentMgr.hpp"

class pIoTServerDB;

class NotificationMgr {
public:
    static NotificationMgr* shared();

    bool begin(pIoTServerDB* db);
    void stop();

    bool isSetup() const;
    bool isEnabled() const;

    /*
     * Queue a direct human-facing notification.
     *
     * This is for action-triggered/manual/system messages where the caller
     * already knows the exact title and body that should appear on the phone.
     * It does not create or modify an incident.
     */
    bool enqueueNotification(const std::string& title,
                             const std::string& message,
                             IncidentMgr::Severity severity = IncidentMgr::Severity::Error);

    bool enqueueIncident(const std::string& source,
                         IncidentMgr::Severity severity,
                         const std::string& code,
                         const std::string& key,
                         const char* message = nullptr,
                         const char* details = nullptr);

    bool enqueueClear(const std::string& source,
                      IncidentMgr::Severity severity,
                      const std::string& code,
                      const std::string& key,
                      const char* message = nullptr,
                      const char* details = nullptr);

private:
    struct PendingNotification {
        std::string action;
        IncidentMgr::Severity severity = IncidentMgr::Severity::Info;

        std::string source;
        std::string code;
        std::string key;

        std::string title;
        std::string message;
        std::string url;
        std::string urlTitle;

        uint64_t createdAt = 0;
        uint64_t nextAttemptAt = 0;
        uint32_t attempts = 0;
    };

    NotificationMgr();
    ~NotificationMgr() = default;

    NotificationMgr(const NotificationMgr&) = delete;
    NotificationMgr& operator=(const NotificationMgr&) = delete;

    bool loadConfig();

    bool enqueueIncidentNotification(const std::string& action,
                                     const std::string& source,
                                     IncidentMgr::Severity severity,
                                     const std::string& code,
                                     const std::string& key,
                                     const char* message,
                                     const char* details);

    void workerMain();

    bool processOneDueNotification();
    bool sendPushover(const PendingNotification& item,
                      std::string& errorText,
                      bool& permanentFailure);

    bool wlan0LooksUsable() const;

    std::string makeTitle(const std::string& action,
                          IncidentMgr::Severity severity,
                          const std::string& source,
                          const std::string& code,
                          const std::string& key) const;

    std::string makeMessage(const std::string& source,
                            const std::string& code,
                            const std::string& key,
                            const char* message,
                            const char* details) const;

    std::string severityToString(IncidentMgr::Severity severity) const;

    int pushoverPriorityForSeverity(IncidentMgr::Severity severity,
                                    bool isClear) const;

    static uint64_t nowSeconds();
    static uint64_t retryDelayForAttempt(uint32_t attempts);

    static NotificationMgr* _sharedInstance;

    mutable std::mutex _mutex;
    std::condition_variable _cv;
    std::thread _worker;
    std::atomic<bool> _running { false };

    pIoTServerDB* _db = nullptr;
    bool _isSetup = false;
    bool _enabled = false;

    std::string _pushoverToken;
    std::string _pushoverUser;
    std::string _defaultUrl;
    std::string _defaultUrlTitle = "Open Farm";

    IncidentMgr::Severity _minSeverity = IncidentMgr::Severity::Error;

    std::deque<PendingNotification> _queue;
};
