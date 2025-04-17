//
//  REST_URL.hpp
//  FOOServer
//
//  Created by Vincent Moscaritolo on 5/4/21.
//

#ifndef REST_URL_hpp
#define REST_URL_hpp

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <functional>
#include <map>
#include <string>
#include "json.hpp"

#include "http_parser.h"

using namespace std;
using namespace nlohmann;

class REST_URL {
	
public:
	
	typedef std::function< void ()> restCallback_t;
 
	~REST_URL();
	REST_URL();
	
	REST_URL(string urlString);
 
	void clear();

	void setURL(string urlString) {	 processData(urlString.c_str(), urlString.size());};
	void setCallBack(restCallback_t cb) {_completion = cb; };
	
	size_t processData(const void *buffer, size_t length, bool shouldHashData = false);

 	bool 						isValid() 		{return _valid;};
	

	enum http_method			 method()			{return _method;};
	vector<string>			    &path() 			{return _path;};
	json 						&body()			{return _body;};
	map<string, string> 	&queries() 		{return _queries;};
	map<string, string> 	&headers() 		{return _headers;};
    
    string  pathString() {return _pathString;};
	string 	bodyHash() 	{return _bodyHashHex;};
		
private:

	void commonInit();
 
	bool 						_valid;
	enum http_method			_method;
	vector<string> 			_path;
    string                  _pathString;
	json 						_body;
	map<string, string> 	_queries;
	map<string, string> 	_headers;

	bool						_calculateHash;
	string						_bodyHashHex;
 
	restCallback_t _completion;
	
	http_parser 				_parser;
	http_parser_settings 	_hpCB;
	char* 					_urlBytes;		// must dealloc this
	char* 					_bodyBytes;		// must dealloc this
	
    typedef enum
    {
        kHdr_Invalid   = 0,
        kHdr_Key,
        kHdr_Value,
    } hdrState_t;

    hdrState_t      _hdrState;
    string          _hdrKey;
    string          _hdrValue;
    
	struct Private;		// for C functions
};

#endif /* REST_URL_hpp */
