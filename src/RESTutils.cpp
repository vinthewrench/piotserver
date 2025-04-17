//
//  RESTutils.cpp

//
//  Created by Vincent Moscaritolo on 4/7/21.
//

#include "RESTutils.hpp"

#include <sstream>

// MARK: - RESTserver utility function
using namespace std;

namespace rest{

string IPAddrString(sockaddr_storage addr){
	std::ostringstream oss;
	
	//Print incoming connection
	if (addr.ss_family == AF_INET){				//IPv4
		
		struct sockaddr_in * v4Addr = (sockaddr_in*) &addr;
		char ip_as_string[INET_ADDRSTRLEN];
		inet_ntop(addr.ss_family,&((struct sockaddr_in *)&addr)->sin_addr,ip_as_string, INET_ADDRSTRLEN);
		oss << "IPv4: " << ip_as_string << ":" << to_string(v4Addr->sin_port);
		
	} else if(addr.ss_family == AF_INET6){ 		//IPv6
		
		struct sockaddr_in6 * v6Addr = (sockaddr_in6*) &addr;
		char ip_as_string[INET6_ADDRSTRLEN];
		inet_ntop(addr.ss_family,&((struct sockaddr_in6 *)&addr)->sin6_addr,ip_as_string, INET6_ADDRSTRLEN);
		oss << "IPv6: " << ip_as_string  << ":" << to_string(v6Addr->sin6_port);
	}
	
	return oss.str();
}

void makeStatusJSON(json &j,
						  httpStatusCodes_t status,
						  string message ,
						  string detail ,
						  string instance,
						  string help) {
	
	bool sucess = (status < 400 );
	
	j[kREST_success] = sucess;
	
	if(!sucess){
		j[kREST_error] [kREST_errorStatus] = status;
		
		if(!message.empty()){
			j[kREST_error] [kREST_errorMessage] = message;
		}
		if(!detail.empty()){
			j[kREST_error] [kREST_errorDetail] = detail;
		}
		if(!help.empty()){
			j[kREST_error] [kREST_errorHelp] = help;
		}
		if(!instance.empty()){
			j[kREST_error] [kREST_errorInstance] = instance;
		}
	}
}


bool didSucceed(json j){
	bool success = false;
	
	if(j.contains(kREST_success)
		&& j.at(kREST_success).is_boolean()){
		success = j.at(kREST_success);
	}
	
	return success;
}

string errorMessage( json j){
	string msg;
	
	if(j.contains(kREST_error)
		&& j.at(kREST_error).is_structured()){
		auto jErr = j.at(kREST_error);
		
        if(jErr.contains(kREST_errorInstance)
			&& jErr.at(kREST_errorInstance).is_string()){
			string message =  jErr.at(kREST_errorInstance);
			msg += message;
		}
		else if(jErr.contains(kREST_errorDetail)
				  && jErr.at(kREST_errorDetail).is_string()){
			string message =  jErr.at(kREST_errorDetail);
			msg += message;
		}
		else if(jErr.contains(kREST_errorMessage)
				  && jErr.at(kREST_errorMessage).is_string()){
			string message =  jErr.at(kREST_errorMessage);
			msg += message;
		}
		else {
			msg += "Unknown error";
		}
	}
	
	return msg;
}


inline unsigned char from_hex (
										 unsigned char ch
										 )
{
	if (ch <= '9' && ch >= '0')
		ch -= '0';
	else if (ch <= 'f' && ch >= 'a')
		ch -= 'a' - 10;
	else if (ch <= 'F' && ch >= 'A')
		ch -= 'A' - 10;
	else
		ch = 0;
	return ch;
}

const std::string urldecode ( const std::string& str )
{
	using namespace std;
	string result;
	string::size_type i;
	for (i = 0; i < str.size(); ++i)
	{
		if (str[i] == '+')
		{
			result += ' ';
		}
		else if (str[i] == '%' && str.size() > i+2)
		{
			const unsigned char ch1 = from_hex(str[i+1]);
			const unsigned char ch2 = from_hex(str[i+2]);
			const unsigned char ch = (ch1 << 4) | ch2;
			result += ch;
			i += 2;
		}
		else
		{
			result += str[i];
		}
	}
	return result;
}

void parse_query(
					std::string word,
					map<string, string>& queries
					)
/*!
 Parses the query string of a URL.  word should be the stuff that comes
 after the ? in the query URL.
 !*/
{
	std::string::size_type pos;
	
	for (pos = 0; pos < word.size(); ++pos)
	{
		if (word[pos] == '&')
			word[pos] = ' ';
	}
	
	std::istringstream sin(word);
	sin >> word;
	while (sin)
	{
		pos = word.find_first_of("=");
		if (pos != std::string::npos)
		{
			std::string key = urldecode(word.substr(0,pos));
			std::string value = urldecode(word.substr(pos+1));
			
			queries[key] = value;
		}
		sin >> word;
	}
}
}


