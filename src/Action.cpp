//
//  Action.cpp
//  coopserver
//
//  Created by Vincent Moscaritolo on 12/29/21.
//
 
#include "EventAction.hpp"
#include "Utils.hpp"

using namespace nlohmann;
using namespace std;


Action::Action(){
	_cmd = string();
	_key = string();
    _value = string();
    _expression = string();
}

Action::Action(string cmd, string key, string value){
	_cmd = cmd;
    _key = key;
    _value = value;
}
 
Action::Action(actionCallback_t cb){
    _cmd = JSON_CMD_CALLBACK;
    _cb = cb;
 }

Action::Action(json j) {
	initWithJSON(j);
}


Action::Action(std::string str){
	_cmd = string();
	_key = string();
    _value = string();
    _expression = string();

	json j;
	j  = json::parse(str);
	initWithJSON(j);
}

void Action::initWithJSON(nlohmann::json j){
	_cmd = string();
	_key = string();
    _value = string();

	if( j.contains(string(JSON_CMD))
		&& j.at(string(JSON_CMD)).is_string()){
 		string str  = j.at(string(JSON_CMD));
 		std::transform(str.begin(), str.end(), str.begin(), ::toupper);
		_cmd = str;
	}
	
    if( j.contains(string(JSON_ACTION_KEY))
		&& j.at(string(JSON_ACTION_KEY)).is_string()){
		string str  = j.at(string(JSON_ACTION_KEY));
        // don't futz with the case
// 		std::transform(str.begin(), str.end(), str.begin(), ::tolower);
		_key = str;
	}

    if( j.contains(JSON_ACTION_VALUE)) {
        auto item = j[JSON_ACTION_VALUE];
        if(item.is_number())
            _value = to_string(item);
        else if(item.is_boolean())
            _value = to_string(item);
        else  if(item.is_string())
            _value = item;
    }
    else  if( j.contains(JSON_ACTION_EXPRESSION)) {
        auto item = j[JSON_ACTION_EXPRESSION];
        
        if(item.is_string())
            _expression = item;
        
    }
        
 }
	
const nlohmann::json Action::JSON(){
	json j;
    
    j[string(JSON_CMD)] = _cmd;
    if(_key.size())
        j[string(JSON_ACTION_KEY)] = _key;
    
    if(_value.size())
        j[string(JSON_ACTION_VALUE)] = _value;
 
    if(_expression.size())
        j[string(JSON_ACTION_EXPRESSION)] = _expression;

 	return j;
}

std::string Action::printString() const {
	std::ostringstream oss;

    oss << _cmd <<  "(";
    
    if(_key.size())
        oss << _key;
  
    if(_key.size() && _value.size())
        oss << ",";

     if(_value.size())
          oss << _value;
 
    if(_key.size() && _expression.size())
        oss << ",";
    
    if(_expression.size())
         oss << _expression;

    oss << ")";
 
   	return  oss.str();

}

bool Action::invokeCallBack(EventTrigger trig) {
    
    bool result = false;
    if(isCallBack()){
        result = (_cb)(trig);
    }
 
    return result;
}
