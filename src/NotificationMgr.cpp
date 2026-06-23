//
//  NotificationMgr.cpp
//  pIoTServer
//

#include "NotificationMgr.hpp"

#include "pIoTServerDB.hpp"
#include "LogMgr.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <utility>

#include <curl/curl.h>

using namespace std;

static constexpr const char* PUSHOVER_API_URL = "https://api.pushover.net/1/messages.json";

static size_t curlWriteDiscard(char* ptr, size_t size, size_t nmemb, void* userdata) {
    (void)ptr;
    (void)userdata;
    return size * nmemb;
}

static string jsonEscape(const string& input) {
    ostringstream ss;

    for(unsigned char c : input) {
        switch(c) {
            case '"':
                ss << "\\\"";
                break;

            case '\\':
                ss << "\\\\";
                break;

            case '\b':
                ss << "\\b";
                break;

            case '\f':
                ss << "\\f";
                break;

            case '\n':
                ss << "\\n";
                break;

            case '\r':
                ss << "\\r";
                break;

            case '\t':
                ss << "\\t";
                break;

            default:
                if(c < 0x20) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    ss << buf;
                }
                else {
                    ss << c;
                }
                break;
        }
    }

    return ss.str();
}

NotificationMgr* NotificationMgr::_sharedInstance = nullptr;

NotificationMgr* NotificationMgr::shared() {
    if(_sharedInstance == nullptr) {
        _sharedInstance = new NotificationMgr();
    }

    return _sharedInstance;
}

NotificationMgr::NotificationMgr() {
}

bool NotificationMgr::begin(pIoTServerDB* db) {
    lock_guard<mutex> lock(_mutex);

    if(_isSetup) {
        return true;
    }

    _db = db;

    if(_db == nullptr) {
        LOGT_ERROR("NotificationMgr begin failed: database is null");
        return false;
    }

    loadConfig();

    _running = true;
    _worker = thread(&NotificationMgr::workerMain, this);

    _isSetup = true;

    if(_enabled) {
        LOGT_INFO("NotificationMgr started: Pushover enabled");
    }
    else {
        LOGT_INFO("NotificationMgr started: notifications disabled");
    }

    return true;
}

void NotificationMgr::stop() {
    {
        lock_guard<mutex> lock(_mutex);

        if(!_isSetup) {
            return;
        }

        _running = false;
        _cv.notify_all();
    }

    if(_worker.joinable()) {
        _worker.join();
    }

    {
        lock_guard<mutex> lock(_mutex);

        _queue.clear();
        _db = nullptr;
        _isSetup = false;
        _enabled = false;
        _pushoverToken.clear();
        _pushoverUser.clear();
        _defaultUrl.clear();
        _defaultUrlTitle = "Open Farm";
    }

    LOGT_INFO("NotificationMgr stopped");
}

bool NotificationMgr::isSetup() const {
    lock_guard<mutex> lock(_mutex);
    return _isSetup;
}

bool NotificationMgr::isEnabled() const {
    lock_guard<mutex> lock(_mutex);
    return _enabled;
}

bool NotificationMgr::enqueueNotification(const string& title,
                                          const string& message,
                                          IncidentMgr::Severity severity) {
    lock_guard<mutex> lock(_mutex);

    if(!_isSetup) {
        return false;
    }

    if(!_enabled) {
        return false;
    }

    if(static_cast<int>(severity) < static_cast<int>(_minSeverity)) {
        return false;
    }

    const uint64_t now = nowSeconds();

    PendingNotification item;
    item.action = "NOTIFY";
    item.severity = severity;
    item.title = title.empty() ? "Farm Notification" : title;
    item.message = message.empty() ? "pIoTServer notification" : message;
    item.url = _defaultUrl;
    item.urlTitle = _defaultUrlTitle;
    item.createdAt = now;
    item.nextAttemptAt = now;
    item.attempts = 0;

    _queue.push_back(std::move(item));
    _cv.notify_all();

    return true;
}

bool NotificationMgr::enqueueIncident(const string& source,
                                      IncidentMgr::Severity severity,
                                      const string& code,
                                      const string& key,
                                      const char* message,
                                      const char* details) {
    return enqueueIncidentNotification("RAISE",
                                       source,
                                       severity,
                                       code,
                                       key,
                                       message,
                                       details);
}

bool NotificationMgr::enqueueClear(const string& source,
                                   IncidentMgr::Severity severity,
                                   const string& code,
                                   const string& key,
                                   const char* message,
                                   const char* details) {
    return enqueueIncidentNotification("CLEAR",
                                       source,
                                       severity,
                                       code,
                                       key,
                                       message,
                                       details);
}

bool NotificationMgr::loadConfig() {
    _enabled = false;
    _pushoverToken.clear();
    _pushoverUser.clear();
    _defaultUrl.clear();
    _defaultUrlTitle = "Open Farm";
    _minSeverity = IncidentMgr::Severity::Error;

    if(_db == nullptr) {
        LOGT_ERROR("NotificationMgr config: database is null");
        return false;
    }

    string value;

    if(_db->getConfigProperty("notifications.enabled", value)) {
        if(value == "1" || value == "true" || value == "TRUE" || value == "yes" || value == "YES") {
            _enabled = true;
        }
    }

    if(_db->getConfigProperty("notifications.pushover.token", value)) {
        _pushoverToken = value;
    }

    if(_db->getConfigProperty("notifications.pushover.user", value)) {
        _pushoverUser = value;
    }

    if(_db->getConfigProperty("notifications.default_url", value)) {
        _defaultUrl = value;
    }

    if(_db->getConfigProperty("notifications.default_url_title", value)) {
        if(!value.empty()) {
            _defaultUrlTitle = value;
        }
    }

    if(_db->getConfigProperty("notifications.min_severity", value)) {
        if(value == "info" || value == "INFO") {
            _minSeverity = IncidentMgr::Severity::Info;
        }
        else if(value == "notice" || value == "NOTICE") {
            _minSeverity = IncidentMgr::Severity::Notice;
        }
        else if(value == "warning" || value == "WARNING") {
            _minSeverity = IncidentMgr::Severity::Warning;
        }
        else if(value == "error" || value == "ERROR") {
            _minSeverity = IncidentMgr::Severity::Error;
        }
        else if(value == "critical" || value == "CRITICAL") {
            _minSeverity = IncidentMgr::Severity::Critical;
        }
    }


    if(_pushoverToken.empty() || _pushoverUser.empty()) {
        _enabled = false;
        LOGT_INFO("NotificationMgr disabled: missing notifications.pushover.token or notifications.pushover.user");
    }

    return true;
}

bool NotificationMgr::enqueueIncidentNotification(const string& action,
                                                  const string& source,
                                                  IncidentMgr::Severity severity,
                                                  const string& code,
                                                  const string& key,
                                                  const char* message,
                                                  const char* details) {
    lock_guard<mutex> lock(_mutex);

    if(!_isSetup) {
        return false;
    }

    if(!_enabled) {
        return false;
    }

    if(static_cast<int>(severity) < static_cast<int>(_minSeverity)) {
        return false;
    }

    const uint64_t now = nowSeconds();

    PendingNotification item;
    item.action = action;
    item.severity = severity;
    item.source = source;
    item.code = code;
    item.key = key;
    item.title = makeTitle(action, severity, source, code, key);
    item.message = makeMessage(source, code, key, message, details);
    item.url = _defaultUrl;
    item.urlTitle = _defaultUrlTitle;
    item.createdAt = now;
    item.nextAttemptAt = now;
    item.attempts = 0;

    _queue.push_back(std::move(item));
    _cv.notify_all();

    return true;
}

bool NotificationMgr::sendPushover(const PendingNotification& item,
                                   string& errorText,
                                   bool& permanentFailure) {
    permanentFailure = false;
    errorText.clear();

    CURL* curl = curl_easy_init();

    if(curl == nullptr) {
        errorText = "curl_easy_init failed";
        return false;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    const bool isClear = item.action == "CLEAR";
    const int priority = pushoverPriorityForSeverity(item.severity, isClear);

    ostringstream json;

    json << "{";
    json << "\"token\":\"" << jsonEscape(_pushoverToken) << "\",";
    json << "\"user\":\"" << jsonEscape(_pushoverUser) << "\",";
    json << "\"title\":\"" << jsonEscape(item.title) << "\",";
    json << "\"message\":\"" << jsonEscape(item.message) << "\",";
    json << "\"priority\":" << priority;

    if(!item.url.empty()) {
        json << ",\"url\":\"" << jsonEscape(item.url) << "\"";
        json << ",\"url_title\":\"" << jsonEscape(item.urlTitle) << "\"";
    }

    if(priority == 2) {
        json << ",\"retry\":60";
        json << ",\"expire\":600";
    }

    json << "}";

    const string jsonBody = json.str();

    curl_easy_setopt(curl, CURLOPT_URL, PUSHOVER_API_URL);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(jsonBody.size()));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "pIoTServer NotificationMgr/1.0");

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteDiscard);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, nullptr);

    CURLcode res = curl_easy_perform(curl);

    long httpStatus = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if(res != CURLE_OK) {
        errorText = curl_easy_strerror(res);
        return false;
    }

    if(httpStatus == 200) {
        return true;
    }

    if(httpStatus == 400 || httpStatus == 401 || httpStatus == 403) {
        permanentFailure = true;
    }

    errorText = "HTTP status " + to_string(httpStatus);
    return false;
}

void NotificationMgr::workerMain() {
    while(_running) {
        bool didWork = processOneDueNotification();

        if(didWork) {
            continue;
        }

        unique_lock<mutex> lock(_mutex);

        if(!_running) {
            break;
        }

        uint64_t now = nowSeconds();
        uint64_t waitSeconds = 30;

        for(const auto& item : _queue) {
            if(item.nextAttemptAt <= now) {
                waitSeconds = 1;
                break;
            }

            const uint64_t delta = item.nextAttemptAt - now;
            if(delta < waitSeconds) {
                waitSeconds = delta;
            }
        }

        _cv.wait_for(lock, chrono::seconds(waitSeconds));
    }
}

bool NotificationMgr::processOneDueNotification() {
    PendingNotification item;

    {
        lock_guard<mutex> lock(_mutex);

        if(!_enabled || _queue.empty()) {
            return false;
        }

        const uint64_t now = nowSeconds();

        auto it = _queue.end();

        for(auto scan = _queue.begin(); scan != _queue.end(); ++scan) {
            if(scan->nextAttemptAt <= now) {
                it = scan;
                break;
            }
        }

        if(it == _queue.end()) {
            return false;
        }

        item = *it;
        _queue.erase(it);
    }

    if(!wlan0LooksUsable()) {
        item.attempts++;
        item.nextAttemptAt = nowSeconds() + retryDelayForAttempt(item.attempts);

        {
            lock_guard<mutex> lock(_mutex);
            _queue.push_back(std::move(item));
            _cv.notify_all();
        }

        return true;
    }

    string errorText;
    bool permanentFailure = false;

    const bool sent = sendPushover(item, errorText, permanentFailure);

    if(sent) {
        LOGT_INFO("NotificationMgr sent notification: %s %s %s",
                  item.action.c_str(),
                  item.source.c_str(),
                  item.code.c_str());
        return true;
    }

    if(permanentFailure) {
        LOGT_ERROR("NotificationMgr permanent notification failure: %s", errorText.c_str());
        return true;
    }

    item.attempts++;
    item.nextAttemptAt = nowSeconds() + retryDelayForAttempt(item.attempts);

    LOGT_ERROR("NotificationMgr notification send failed, retry scheduled in %llu seconds: %s",
               static_cast<unsigned long long>(retryDelayForAttempt(item.attempts)),
               errorText.c_str());

    {
        lock_guard<mutex> lock(_mutex);
        _queue.push_back(std::move(item));
        _cv.notify_all();
    }

    return true;
}

bool NotificationMgr::wlan0LooksUsable() const {
    FILE* fp = popen("/sbin/ip -4 addr show dev wlan0 scope global 2>/dev/null | /usr/bin/grep -q 'inet '", "r");

    if(fp == nullptr) {
        return true;
    }

    const int addrStatus = pclose(fp);

    if(addrStatus != 0) {
        return false;
    }

    fp = popen("/sbin/ip route get 1.1.1.1 2>/dev/null | /usr/bin/grep -q 'dev wlan0'", "r");

    if(fp == nullptr) {
        return true;
    }

    const int routeStatus = pclose(fp);

    return routeStatus == 0;
}

string NotificationMgr::makeTitle(const string& action,
                                  IncidentMgr::Severity severity,
                                  const string& source,
                                  const string& code,
                                  const string& key) const {
    ostringstream ss;

    if(action == "CLEAR") {
        ss << "Farm Recovered";
    }
    else if(severity == IncidentMgr::Severity::Critical) {
        ss << "Farm Critical Alert";
    }
    else {
        ss << "Farm Alert";
    }

    if(!code.empty()) {
        ss << ": " << code;
    }

    if(!key.empty()) {
        ss << " " << key;
    }

    if(!source.empty()) {
        ss << " [" << source << "]";
    }

    return ss.str();
}

string NotificationMgr::makeMessage(const string& source,
                                    const string& code,
                                    const string& key,
                                    const char* message,
                                    const char* details) const {
    ostringstream ss;

    if(message != nullptr && strlen(message) > 0) {
        ss << message;
    }
    else {
        ss << code;
    }

    if(!key.empty()) {
        ss << "\nKey: " << key;
    }

    if(!source.empty()) {
        ss << "\nSource: " << source;
    }

    if(details != nullptr && strlen(details) > 0) {
        ss << "\nDetails: " << details;
    }

    return ss.str();
}

string NotificationMgr::severityToString(IncidentMgr::Severity severity) const {
    switch(severity) {
        case IncidentMgr::Severity::Info:
            return "info";

        case IncidentMgr::Severity::Notice:
            return "notice";

        case IncidentMgr::Severity::Warning:
            return "warning";

        case IncidentMgr::Severity::Error:
            return "error";

        case IncidentMgr::Severity::Critical:
            return "critical";
    }

    return "unknown";
}

int NotificationMgr::pushoverPriorityForSeverity(IncidentMgr::Severity severity,
                                                 bool isClear) const {
    if(isClear) {
        return 0;
    }

    switch(severity) {
        case IncidentMgr::Severity::Info:
            return -1;

        case IncidentMgr::Severity::Notice:
            return 0;

        case IncidentMgr::Severity::Warning:
            return 1;

        case IncidentMgr::Severity::Error:
            return 1;

        case IncidentMgr::Severity::Critical:
            return 2;
    }

    return 0;
}

uint64_t NotificationMgr::nowSeconds() {
    using namespace chrono;

    return duration_cast<seconds>(
        system_clock::now().time_since_epoch()
    ).count();
}

uint64_t NotificationMgr::retryDelayForAttempt(uint32_t attempts) {
    switch(attempts) {
        case 0:
        case 1:
            return 60;

        case 2:
            return 600;

        default:
            return 3600;
    }
}
