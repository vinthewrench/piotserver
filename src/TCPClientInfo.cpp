//
//  TCPClientInfo.cpp

//
//  Created by Vincent Moscaritolo on 4/7/21.
//

#include <stdio.h>
#include <sstream>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "TCPClientInfo.hpp"

using namespace std;

TCPClientInfo::TCPClientInfo(clientType_t typ, const string connName) {
	_name = connName;
	_type = typ;
	_headers.clear();
	
};

std::string TCPClientInfo::remoteAddrString(){
	std::ostringstream oss;
 
	//Print incoming connection
	if (_remoteAddr.ss_family == AF_INET){				//IPv4
		
		struct sockaddr_in * v4Addr = (sockaddr_in*) &_remoteAddr;
		char ip_as_string[INET_ADDRSTRLEN];
		inet_ntop(_remoteAddr.ss_family,&((struct sockaddr_in *)&_remoteAddr)->sin_addr,ip_as_string, INET_ADDRSTRLEN);
		oss << "IPv4: " << ip_as_string << ":" << to_string(v4Addr->sin_port);
		
	} else if(_remoteAddr.ss_family == AF_INET6){ 		//IPv6
		
		struct sockaddr_in6 * v6Addr = (sockaddr_in6*) &_remoteAddr;
		char ip_as_string[INET6_ADDRSTRLEN];
		inet_ntop(_remoteAddr.ss_family,&((struct sockaddr_in6 *)&_remoteAddr)->sin6_addr,ip_as_string, INET6_ADDRSTRLEN);
		oss << "IPv6: " << ip_as_string  << ":" << to_string(v6Addr->sin6_port);
	}

	return oss.str();
}
