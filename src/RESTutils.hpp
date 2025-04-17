//
//  RESTutils.hpp

//
//  Created by Vincent Moscaritolo on 4/7/21.
//

#ifndef RESTutils_hpp
#define RESTutils_hpp

#include <stdio.h>
#include "json.hpp"
#include <netinet/in.h>
#include <arpa/inet.h>

#include "http_parser.h"

#include <map>

using namespace nlohmann;
using namespace std;

typedef enum {
	STATUS_OK 					= 200,
	STATUS_NO_CONTENT			= 204,
	STATUS_NOT_MODIFIED		= 304,
	STATUS_BAD_REQUEST			= 400,
	STATUS_ACCESS_DENIED		= 401,
	STATUS_INVALID_BODY		= 4006,
	STATUS_NOT_FOUND			= 404,
	STATUS_INVALID_METHOD	= 405,
	STATUS_CONFLICT			= 409,
	STATUS_INTERNAL_ERROR		= 500,
	STATUS_NOT_IMPLEMENTED	= 501,
	STATUS_UNAVAILABLE		= 503,
 
}httpStatusCodes_t;

typedef int httpMethods_t;

namespace rest{

constexpr const char* kREST_command	  			= "cmd" ;

constexpr const char* kREST_success		  		= "success" ;

constexpr const char* kREST_error 			 	= "error" ;
constexpr const char* kREST_errorStatus 		 	= "status" ;
constexpr const char* kREST_errorMessage   	= "title" ;
constexpr const char* kREST_errorDetail  		= "detail" ;
constexpr const char* kREST_errorInstance  	= "instance" ;
constexpr const char* kREST_errorHelp  		= "help" ;

void makeStatusJSON(json &j,
                    httpStatusCodes_t status,
                    string message = "",
                    string detail = "",
                    string instance = "",
                    string help = "");

string errorMessage(json reply);
bool 	didSucceed(json reply);

string IPAddrString(sockaddr_storage addr);


/*!
 Parses the query string of a URL.  word should be the stuff that comes
 after the ? in the query URL.
 !*/
void parse_query( std::string word, map<string, string>& queries);

const std::string urldecode ( const std::string& str );

};

#endif /* RESTutils_hpp */
