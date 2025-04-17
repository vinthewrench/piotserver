//
//  RESTServerConnection.hpp

//
//  Created by Vincent Moscaritolo on 3/13/21.
//

#ifndef RESTServerConnection_hpp
#define RESTServerConnection_hpp

#include <map>
#include <string>

#include "TCPServer.hpp"
#include "http_parser.h"

#include "REST_URL.hpp"

class RESTServerConnection : public TCPServerConnection {
	
public:

	RESTServerConnection();
	~RESTServerConnection();

	virtual void didOpen();
	
	virtual void willClose();
	virtual void didRecvData(const void *buffer, size_t length);

	// TCPServerConnection passthrough
	
	virtual void sendString(const std::string);
	virtual void closeConnection() ;
	virtual bool isConnected();
 
private:
	std::string httpHeaderForStatusCode(httpStatusCodes_t code);
	std::string httpHeaderForContentLength(size_t length);

	bool validateRequestCredentials();
 
	REST_URL				_rURL;
	bool					_isOpen;
};

#endif /* RESTServerConnection_hpp */
