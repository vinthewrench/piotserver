//
//  ServerCmdQueue.hpp

//
//  Created by Vincent Moscaritolo on 3/19/21.
//

#ifndef ServerCmdQueue_hpp
#define ServerCmdQueue_hpp

#include <stdio.h>
#include <stdint.h>
#include <string>
#include <functional>
#include <vector>
#include <list>
#include <map>
#include <tuple>
#include <any>

#include <string_view>
 
#include "CommonDefs.hpp"
#include "RESTutils.hpp"
#include "TCPClientInfo.hpp"
#include "REST_URL.hpp"


using namespace nlohmann;
using namespace std;

class APISecretMgr {

public:
	
	virtual ~APISecretMgr(){};
	virtual bool apiSecretCreate(string APIkey, string APISecret) = 0;
	virtual bool apiSecretDelete(string APIkey) = 0;
	virtual bool apiSecretGetSecret(string APIkey, string &APISecret) = 0;
	virtual bool apiSecretMustAuthenticate() = 0;
};


class ServerCmdQueue {

public:
	
	typedef std::function<void(	json reply,
										httpStatusCodes_t code)> cmdCallback_t;

	static ServerCmdQueue *shared() {
				if(!sharedInstance)
					throw std::runtime_error("ServerCmdQueue not setup");
			return sharedInstance;
		}

	static ServerCmdQueue *sharedInstance;

	ServerCmdQueue(APISecretMgr *apiSecretmgr);
	~ServerCmdQueue();

	void queueRESTCommand( REST_URL url,
								TCPClientInfo cInfo,
								cmdCallback_t completion );

	typedef std::function<void(ServerCmdQueue* ref,
										REST_URL url,
										TCPClientInfo cInfo,
										cmdCallback_t completion)> nounHandler_t;
	
	bool registerNoun(	string_view noun,
							nounHandler_t handler = NULL );

	bool apiSecretCreate(string APIkey, string APISecret);
	bool apiSecretDelete(string APIkey);
	bool apiSecretGetSecret(string APIkey, string &APISecret);
	bool apiSecretLoad();
	bool apiSecretSave();
	bool apiSecretMustAuthenticate();

private:
	
	map<string_view,  	nounHandler_t> _nounHandlers;
	nounHandler_t 		handlerForNoun(string noun);
	APISecretMgr* 		_apiSecretMgr;
};

#endif /* ServerCmdQueue_hpp */
