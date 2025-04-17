//  EventTrigger..cpp
//  coopserver
//
//  Created by Vincent Moscaritolo on 12/29/21.
//

#include <chrono>
#include "croncpp.hpp"
#include <iostream>

#include "TimeStamp.hpp"
#include "PropValKeys.hpp"
#include "CommonDefs.hpp"  // for stringvector

#include "EventAction.hpp"

using namespace nlohmann;
 
constexpr string_view dayOfWeekStrings[] =
                {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday","All"};
constexpr string_view dayOfWeekShortStrings[] =
                {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "All"};


string_view preprocess_cronstring(string_view expr){
   
    map<CRONCPP_STRING_VIEW, CRONCPP_STRING_VIEW>  macros = {
        
        { "@30sec" ,   "*/30 * * * * ?" },
        { "@minute" ,   "0 * * * * ?" },
        { "@10minute" , "0 */10 * * * ?" },
        { "@hourly" ,   "0 0 * * * *" },
        { "@daily" ,    "0 0 0 * * *" },
        { "@midnight" , "0 0 0 * * *" },
        { "@weekly" ,   "0 0 0 * * 0" },
        { "@monthly",   "0 0 0 1 * *" },
        { "@yearly" ,   "0 0 0 1 1 *" },
        { "@annually" , "0 0 0 1 1 *" },
       };
    
    if(macros.count(expr))
        return macros.at(expr);
    else
        return expr;
 }



// MARK: - EventTrigger()
EventTrigger::EventTrigger(cron_event_t cronEvent){
    _eventType          = EVENT_TYPE_CRON;
    _cronEvent          = cronEvent;
 }
 
EventTrigger::EventTrigger(app_event_t appEvent){
    _eventType          = EVENT_TYPE_APP;
    _appEvent        = appEvent;
}

EventTrigger::EventTrigger(){
    _eventType                      = EVENT_TYPE_UNKNOWN;
}

EventTrigger::EventTrigger(tod_offset_t timeBase, int16_t timeBaseOffset){
    _eventType                      = EVENT_TYPE_TIME;
    _timeEvent.timeBase             = timeBase;
    _timeEvent.timeBaseOffset            = timeBaseOffset;
    _timeEvent.lastRun              = 0;
    _timeEvent.dayOfWeek.byte       = everyDayOfWeek;
}

EventTrigger::EventTrigger(tod_offset_t timeBase, int16_t timeBaseOffset, dayOfWeek_t dayOfWeek){
    _eventType                   = EVENT_TYPE_TIME;
    _timeEvent.timeBase         = timeBase;
    _timeEvent.timeBaseOffset        = timeBaseOffset;
    _timeEvent.lastRun          = 0;
    _timeEvent.dayOfWeek        = dayOfWeek;
}

EventTrigger::EventTrigger(time_t time) { // Ephmeral event
    _eventType                   = EVENT_TYPE_EPHEMERAL;
    _ephmeralTime               = time;
}

 
void EventTrigger::copy(const EventTrigger &evt1, EventTrigger *evt2){
        
    evt2->_eventType         = evt1._eventType;
    
    switch (evt1._eventType) {
        
        case EVENT_TYPE_TIME:
            evt2->_timeEvent = evt1._timeEvent;
                break;

        case EVENT_TYPE_APP:
            evt2->_appEvent = evt1._appEvent;
                break;

        case EVENT_TYPE_EPHEMERAL:
            evt2->_ephmeralTime = evt1._ephmeralTime;
                break;

        case EVENT_TYPE_CRON:
            evt2->_cronEvent = evt1._cronEvent;
                break;

        default:
            break;
    }
}

EventTrigger::EventTrigger(nlohmann::json j){
    initWithJSON(j);
}

EventTrigger::EventTrigger(std::string str){
    _eventType = EVENT_TYPE_UNKNOWN;
 
    json j;
    j  = json::parse(str);
    initWithJSON(j);
}

static bool dayOfWeekFromFromString(string str, EventTrigger::dayOfWeek_t &dowOut){
    bool success = false;
    
    for(int i = 0; i < 7; i++){
        if(caseInSensStringCompare(str, string( dayOfWeekStrings[i]))
           || caseInSensStringCompare(str,string( dayOfWeekShortStrings[i]))){
            switch(i){
                case 0: dowOut.day.Sun = true; break;
                case 1: dowOut.day.Mon = true; break;
                case 2: dowOut.day.Tue = true; break;
                case 3: dowOut.day.Wed = true; break;
                case 4: dowOut.day.Thu = true; break;
                case 5: dowOut.day.Fri = true; break;
                case 6: dowOut.day.Sat = true; break;
                case 7: dowOut.byte = EventTrigger::everyDayOfWeek;  break;
            }
            success = true;
            break;
        }
    }
    return success;
}

static bool dayOfWeekFromJSON(json j, EventTrigger::dayOfWeek_t &dowOut){
    
    bool success = false;
    EventTrigger::dayOfWeek_t dow = {0};
    
    if(j.is_string()){
        string str = j;
        
        if(dayOfWeekFromFromString(str, dow)){
            dowOut = dow;
            success = true;
        }
    }
    else if (j.is_array()){
        for(auto j1 :j){
            if(!j1.is_string())  return false;
            string str = j1;
            if(!dayOfWeekFromFromString(str, dow)) return false;
          }
        dowOut = dow;
        success = true;
    }
    
    return success;
}

static string timeStringFromMinutesFromMidnight(int16_t mins){
    struct tm tm{};
    tm.tm_hour =  mins /60;
    tm.tm_min =  mins % 60;
    
    char buffer[16];
    strftime(buffer,sizeof(buffer),"%I:%M %p",&tm);
    return string(buffer);;
}

static bool getMinutesFromMidnightFromJSON(nlohmann::json j, int16_t &mins){
    bool success = false;
    
    if(j.is_number_unsigned()) {
        mins = j;
        success = true;
    }
    else if(j.is_string()) {
        string timsStr = j;
        struct tm time;
        memset(&time, 0, sizeof(struct tm));
        
        if(strptime(timsStr.c_str(), "%I:%M %p", &time)){
            mins =  (time.tm_hour * 60) + time.tm_min;
            success = true;
        }
        else if(strptime(timsStr.c_str(), "%H:%M", &time)){
            mins =  (time.tm_hour * 60) + time.tm_min;
            success = true;
        }
    }
    
    return success;
}

void EventTrigger::initWithJSON(nlohmann::json j){
    
    _eventType = EVENT_TYPE_UNKNOWN;
    
    if(j.contains(JSON_TIME_BASE)) {
        _eventType = EVENT_TYPE_TIME;
    }
    else if(j.contains(JSON_ARG_EVENT)){
        _eventType = EVENT_TYPE_APP;
        _appEvent = APP_EVENT_INVALID;
    }
    else if(j.contains(JSON_TIME_CRON)){
        _eventType = EVENT_TYPE_CRON;
        _cronEvent.cronString = "";
        _cronEvent.next = 0;
    }

    if( j.contains(JSON_TIME_TOD)) {
        if(getMinutesFromMidnightFromJSON(j.at(JSON_TIME_TOD),_timeEvent.timeBaseOffset)){
            _eventType = EVENT_TYPE_TIME;
            _timeEvent.timeBase = TOD_ABSOLUTE;
        }
        else {
            _eventType = EVENT_TYPE_UNKNOWN;
        }
        _timeEvent.lastRun = 0;
    }
    else if(_eventType == EVENT_TYPE_TIME) {
        
        _timeEvent.timeBase = TOD_INVALID;
        _timeEvent.timeBaseOffset = 0;
        _timeEvent.lastRun = 0;
        
        if( j.contains(JSON_TIME_BASE)){
            if(j.at(JSON_TIME_BASE).is_number()) {
                _timeEvent.timeBase = j.at(JSON_TIME_BASE);
            }
            else if (j.at(JSON_TIME_BASE).is_string()){
                string str = Utils::trim(j.at(JSON_TIME_BASE));
                
                if(caseInSensStringCompare(str, string(JSON_ARG_SUNSET))) {
                    _timeEvent.timeBase = TOD_SUNSET;
                }
                else  if(caseInSensStringCompare(str,string(JSON_ARG_SUNRISE))) {
                    _timeEvent.timeBase = TOD_SUNRISE;
                }
                else  if(caseInSensStringCompare(str,string(JSON_ARG_CIVIL_SUNRISE))) {
                    _timeEvent.timeBase = TOD_CIVIL_SUNRISE;
                }
                else  if(caseInSensStringCompare(str,string(JSON_ARG_CIVIL_SUNSET))) {
                    _timeEvent.timeBase = TOD_CIVIL_SUNSET;
                }
            }
        }
        
        if( j.contains(string(JSON_TIME_OFFSET))
           && j.at(string(JSON_TIME_OFFSET)).is_number()){
            _timeEvent.timeBaseOffset = j.at(string(JSON_TIME_OFFSET));
        }
    }
    else if(_eventType == EVENT_TYPE_APP) {
        
        if( j.contains(string(JSON_ARG_EVENT))
           && j.at(string(JSON_ARG_EVENT)).is_string()){
            string str = j.at(string(JSON_ARG_EVENT));
            
            if(str == JSON_EVENT_STARTUP ){
                _appEvent = APP_EVENT_STARTUP;
            }
            else if(str == JSON_EVENT_SHUTDOWN ){
                _appEvent = APP_EVENT_SHUTDOWN;
            }
            else if(str == JSON_EVENT_MANUAL ){
                _appEvent = APP_EVENT_MANUAL;
            }
        }
    }
    else if((_eventType == EVENT_TYPE_CRON)
            && j.contains(JSON_TIME_CRON)
           && j.at(JSON_TIME_CRON).is_string()){
        
        string str = j.at(JSON_TIME_CRON);

        try
        {
            // validate cron string
            auto cron = cron::make_cron(preprocess_cronstring(str));
     
            _cronEvent.cronString = str;
            scheduleNextCronTime();
//            printf ("- %s -\n", printString().c_str());
        }
        catch (cron::bad_cronexpr const & ex)
        {
            cerr << ex.what() << str << '\n';
            _eventType = EVENT_TYPE_UNKNOWN;
         }
    }
            
    if(_eventType == EVENT_TYPE_TIME) {
        dayOfWeek_t dow;
        
        if( j.contains(JSON_TIME_DOW)
           && dayOfWeekFromJSON(j.at(JSON_TIME_DOW), dow))
            _timeEvent.dayOfWeek = dow;
        else _timeEvent.dayOfWeek.byte = everyDayOfWeek;
    }
}

nlohmann::json EventTrigger::JSON(){
    json j;
    
    switch(_eventType){
        case EVENT_TYPE_EPHEMERAL:
        {
            j[JSON_TIME_EPHMERAL] = _ephmeralTime;
        }
         break;
         
        case EVENT_TYPE_CRON:
        {
            j[string(JSON_TIME_CRON)]     =  _cronEvent.cronString;
        }
            break;
        case EVENT_TYPE_TIME:
        {
            json j1;
            
            if(_timeEvent.timeBase == TOD_ABSOLUTE){
                j1[JSON_TIME_TOD] =  timeStringFromMinutesFromMidnight(_timeEvent.timeBaseOffset);
            }
            else {
                switch (_timeEvent.timeBase) {
                    case TOD_SUNSET:
                        j1[JSON_TIME_BASE] = JSON_ARG_SUNSET;
                        break;
                    case TOD_SUNRISE:
                        j1[JSON_TIME_BASE] = JSON_ARG_SUNRISE;
                        break;
                    case TOD_CIVIL_SUNSET:
                        j1[JSON_TIME_BASE] = JSON_ARG_CIVIL_SUNSET;
                        break;
                    case TOD_CIVIL_SUNRISE:
                        j1[JSON_TIME_BASE] = JSON_ARG_CIVIL_SUNRISE;
                        break;
                        
                    default:
                        j1[string(JSON_TIME_BASE)]     =  _timeEvent.timeBase;
                        break;
                }
                if (_timeEvent.timeBaseOffset != 0) {
                    j1[string(JSON_TIME_OFFSET)] =  _timeEvent.timeBaseOffset;
                }
                
            }
            if(_timeEvent.dayOfWeek.byte != everyDayOfWeek){
                stringvector days = {};
                if(_timeEvent.dayOfWeek.day.Sun) days.push_back(string(dayOfWeekStrings[0]));
                if(_timeEvent.dayOfWeek.day.Mon) days.push_back(string(dayOfWeekStrings[1]));
                if(_timeEvent.dayOfWeek.day.Tue) days.push_back(string(dayOfWeekStrings[2]));
                if(_timeEvent.dayOfWeek.day.Wed) days.push_back(string(dayOfWeekStrings[3]));
                if(_timeEvent.dayOfWeek.day.Thu) days.push_back(string(dayOfWeekStrings[4]));
                if(_timeEvent.dayOfWeek.day.Fri) days.push_back(string(dayOfWeekStrings[5]));
                if(_timeEvent.dayOfWeek.day.Sat) days.push_back(string(dayOfWeekStrings[6]));
                
                if(days.size() == 1)
                    j1[JSON_TIME_DOW] = days[0];
                else
                    j1[JSON_TIME_DOW] = days;
            }
            
            j = j1;
        }
            break;
            
        case EVENT_TYPE_APP:
        {
            json j1;
            
            switch (_appEvent) {
                case APP_EVENT_STARTUP:
                    j1[string(JSON_ARG_EVENT)] =  JSON_EVENT_STARTUP;
                    break;
                    
                case APP_EVENT_SHUTDOWN:
                    j1[string(JSON_ARG_EVENT)] =  JSON_EVENT_SHUTDOWN;
                    break;
                    
                case APP_EVENT_MANUAL:
                    j1[string(JSON_ARG_EVENT)] =  JSON_EVENT_MANUAL;
                    break;
    
                default:
                    break;
            }
            j = j1;
        }
            break;

        default:;
    }
    
    return j;
}


const std::string EventTrigger::printString(bool fullString){
    std::ostringstream oss;
    using namespace timestamp;

    auto j = JSON();
    
    if(_eventType == EVENT_TYPE_TIME){
        
        
        solarTimes_t solar;
        SolarTimeMgr::shared()->getSolarEventTimes(solar);
        int16_t minsFromMidnight = 0;
        
        // when does it need to run today
        calculateTriggerTime(solar,minsFromMidnight);
        time_t schedTime = solar.previousMidnight + (minsFromMidnight * SECS_PER_MIN) ;
        
        string timeString = TimeStamp(schedTime).ClockString();
        string offsetStr;
        
        int16_t minutes = abs(_timeEvent.timeBaseOffset);
        if(minutes == 0 ){
            
        }else if(minutes > 59){
            int hours = minutes / 60;
            minutes = minutes % 60;
            offsetStr += to_string(hours) + "h";
            if(minutes > 0)   offsetStr += " " + to_string(minutes) + "m";
         } else  {
             offsetStr += to_string(minutes) + "m";
        }
        
        if(_timeEvent.timeBaseOffset > 0)
            offsetStr = " + " + offsetStr;
        else if(_timeEvent.timeBaseOffset < 0)
            offsetStr = " - " + offsetStr;
      
        switch(_timeEvent.timeBase){
            case TOD_SUNRISE:
                if(fullString)
                    oss << timeString << " (Sunrise"  << offsetStr << ")" ;
                else
                    oss << "Sunrise"  << offsetStr;
                break;
                
            case TOD_SUNSET:
                if(fullString)
                    oss  << timeString << " (Sunset"  << offsetStr << ")" ;
                else
                    oss << "Sunset"  << offsetStr;
                break;
                
            case TOD_CIVIL_SUNRISE:
                if(fullString)
                    oss  << timeString << " (Civil Sunrise"  << offsetStr << ")" ;
                else
                    oss << "Civil Sunrise"  << offsetStr;
                break;
                
            case TOD_CIVIL_SUNSET:
                if(fullString)
                    oss  << timeString << " (Civil Sunset"  << offsetStr << ")" ;
                else
                    oss << "Civil Sunset"  << offsetStr;
                break;
                
            case TOD_ABSOLUTE:
                oss << timeString;
                break;
                
            case TOD_INVALID:
                oss <<  "Invalid Time:";
                break;
        }
    }
     else if(_eventType == EVENT_TYPE_CRON){
        
         time_t now = time(0);
      
         auto cron = cron::make_cron(preprocess_cronstring(_cronEvent.cronString));
          time_t next = cron::cron_next(cron, now);

         oss <<  " (Cron: " << "\"" << _cronEvent.cronString << "\" " ;
         oss  <<  timestamp::TimeStamp(next).ClockString(false) << ")";
    }
    else    {
        oss << j.dump();
    }
    return  oss.str();
}
  
bool EventTrigger::isValid(){
    return (_eventType != EVENT_TYPE_UNKNOWN);
}
bool EventTrigger::isTimed(){
    return (_eventType == EVENT_TYPE_TIME);
}
bool EventTrigger::isEphemeral(){
    return (_eventType == EVENT_TYPE_EPHEMERAL);
}
bool EventTrigger::isCronEvent(){
    return (_eventType == EVENT_TYPE_CRON);
}

bool EventTrigger::isAppEvent(){
    return (_eventType == EVENT_TYPE_APP);
}

bool EventTrigger::getTimedEventInfo(timeEventInfo_t &info){
    if(_eventType == EVENT_TYPE_TIME){
         info = _timeEvent;
        return true;
    }
    else
        return false;
}

bool EventTrigger::getAppEventInfo(app_event_t &info){
    if(_eventType == EVENT_TYPE_APP){
        info = _appEvent;
        return true;
    }
    else
        return false;
}


bool EventTrigger::setLastRun(time_t time){
    if(_eventType == EVENT_TYPE_TIME){
        _timeEvent.lastRun = time;
        return true;
    } else return false;
}

bool EventTrigger::nextCronTime(time_t &next){
    if(_eventType == EVENT_TYPE_CRON){
        next = _cronEvent.next;
        return true;
    } else return false;
}

bool EventTrigger::scheduleNextCronTime(){
    if(_eventType == EVENT_TYPE_CRON){
        time_t now = time(0);
        auto cron = cron::make_cron(preprocess_cronstring(_cronEvent.cronString));
        _cronEvent.next =  cron::cron_next(cron, now);
        return true;
     } else return false;
}


bool EventTrigger::shouldTriggerInFuture(const solarTimes_t &solar, time_t localNow){
    
    bool result = false;
    
    if(_eventType == EVENT_TYPE_TIME && canTriggerOnDay(localNow)){
        
        int16_t minsFromMidnight = 0;
        
        // when does it need to run today
        if(calculateTriggerTime(solar,minsFromMidnight)) {
            time_t schedTime = solar.previousMidnight + (minsFromMidnight * SECS_PER_MIN) ;
            //
            //            printf("\n sched: %s \n", timestamp::TimeStamp(schedTime).ClockString().c_str());
            //            printf(" now:  %s \n", timestamp::TimeStamp(localNow).ClockString().c_str());
            //
            if( schedTime > localNow) {
                result = true;
            }
        };
    }
    else  if(_eventType == EVENT_TYPE_EPHEMERAL){
        time_t now = time(NULL);
        result = (_ephmeralTime > now);
    }
    else  if(_eventType == EVENT_TYPE_CRON){
        time_t now = time(NULL);
        result = (_cronEvent.next > now);
    }
 
    return result;
}

bool EventTrigger::shouldTriggerFromAppEvent(app_event_t a){
    bool result = false;

    if(_eventType == EVENT_TYPE_APP){
        return (_appEvent == a);
    }
    
    return result;
}


bool EventTrigger::canTriggerOnDay(time_t localNow){
    
    bool result = false;
    
    if(_eventType == EVENT_TYPE_TIME){
        
        if(_timeEvent.dayOfWeek.byte == everyDayOfWeek)
            result = true;
        
        else {
            struct tm timeinfo = {0};
            // localNow is already in local time , use gmtime_r to NOT convert it
            gmtime_r(&localNow, &timeinfo);
            
            switch(timeinfo.tm_wday){
                case 0: if(_timeEvent.dayOfWeek.day.Sun) result = true;  break;
                case 1: if(_timeEvent.dayOfWeek.day.Mon) result = true;  break;
                case 2: if(_timeEvent.dayOfWeek.day.Tue) result = true;  break;
                case 3: if(_timeEvent.dayOfWeek.day.Wed) result = true;  break;
                case 4: if(_timeEvent.dayOfWeek.day.Thu) result = true;  break;
                case 5: if(_timeEvent.dayOfWeek.day.Fri) result = true;  break;
                case 6: if(_timeEvent.dayOfWeek.day.Sat) result = true;  break;
                default: break;
            }
        }
        
    }
    return result;
}

bool EventTrigger::shouldTriggerFromTimeEvent(const solarTimes_t &solar, time_t localNow){
    
    bool result = false;
    
    if(_eventType == EVENT_TYPE_TIME && canTriggerOnDay(localNow)){
             
        int16_t minsFromMidnight = 0;
        
        // when does it need to run today
        if(calculateTriggerTime(solar,minsFromMidnight)) {
            time_t schedTime = solar.previousMidnight + (minsFromMidnight * SECS_PER_MIN) ;

//            printf("\n sched: %s \n", timestamp::TimeStamp(schedTime).ClockString().c_str());
//            printf(" now:  %s \n", timestamp::TimeStamp(localNow).ClockString().c_str());

            if( schedTime < localNow) {
                if( _timeEvent.lastRun  == 0
                    ||  _timeEvent.lastRun < solar.previousMidnight )
                    result = true;
            }
        };
    }
   else  if(_eventType == EVENT_TYPE_EPHEMERAL){
       time_t now = time(NULL);
       result = (_ephmeralTime <= now);
    }
    
   else  if(_eventType == EVENT_TYPE_CRON){
       time_t now = time(NULL);
       if(_cronEvent.next < now){
           result = true;
        }
     }
  
    return result;
}


bool EventTrigger::calculateTriggerTime(const solarTimes_t &solar, int16_t &minsFromMidnight) {
    
    bool result = false;
    
    if(_eventType == EVENT_TYPE_TIME){
        int16_t actualTime  = _timeEvent.timeBaseOffset;
        
        switch(_timeEvent.timeBase){
            case TOD_SUNRISE:
                actualTime = solar.sunriseMins + actualTime;
                result = true;
                break;
                
            case TOD_SUNSET:
                actualTime = solar.sunSetMins + actualTime;
                result = true;
                break;
                
            case TOD_CIVIL_SUNRISE:
                actualTime = solar.civilSunRiseMins + actualTime;
                result = true;
                break;
                
            case TOD_CIVIL_SUNSET:
                actualTime = solar.civilSunSetMins + actualTime;
                result = true;
                break;
                
            case TOD_ABSOLUTE:
                actualTime = actualTime;
                result = true;
                break;
                
            case TOD_INVALID:
                break;
        }
        
        if(result)
            minsFromMidnight = actualTime;
    }
 
    return result;
}

