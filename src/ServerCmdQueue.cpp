//
//  ServerCmdQueue.cpp

//
//  Created by Vincent Moscaritolo on 3/19/21.
//

#include <iostream>
#include <iomanip>
#include <regex>
#include <iterator>

#include "json.hpp"
#include "Utils.hpp"


#include "ServerCmdQueue.hpp"
#include "TCPClientInfo.hpp"

using namespace nlohmann;
using namespace std;
 

// MARK: - ServerCmdQueue

ServerCmdQueue *ServerCmdQueue::sharedInstance = 0;

ServerCmdQueue::ServerCmdQueue(APISecretMgr *apiSecretmgr) {
	_nounHandlers.clear();
	_apiSecretMgr = apiSecretmgr;
	sharedInstance = this;
};

ServerCmdQueue::~ServerCmdQueue() {
	_nounHandlers.clear();
};

bool ServerCmdQueue::registerNoun(	string_view noun,
											 nounHandler_t handler ) {
	if(handler)
		_nounHandlers.insert( {noun, handler} );
	return true;
}

ServerCmdQueue::nounHandler_t ServerCmdQueue::handlerForNoun(string noun){
	
	nounHandler_t  handler = NULL;

	auto it = _nounHandlers.find(noun);
	if(it != _nounHandlers.end()){
		handler = it->second;
	}
	
	
 	return handler;
}

// MARK: - command management and processing

void ServerCmdQueue::queueRESTCommand(  REST_URL url,
												  TCPClientInfo cInfo,
												  cmdCallback_t completion  ) {
	
	using namespace rest;
	
	nounHandler_t  func = NULL;

 	auto path = url.path();
//#warning DEBUG
//    printf("ServerCmdQueue::queueRESTCommand %s\n", path[0].c_str());
    
	if(path.size() > 0) {
		string noun = path.at(0);
		func = handlerForNoun(noun);
	}
	
	if(func) {
		(func)(this, url, cInfo, completion);
	}
	else {
		json reply;
		makeStatusJSON(reply,
							STATUS_NOT_IMPLEMENTED,
							"Not Implemented",
                            "Lazy programmer hasn't written code for this yet.",
                             url.pathString());
		
		(completion) (reply, STATUS_NOT_IMPLEMENTED);
		
	}
}

// MARK: - User Authentication

bool ServerCmdQueue::apiSecretCreate(string APIkey, string APISecret){
 
	return (_apiSecretMgr)
		?_apiSecretMgr->apiSecretCreate(APIkey,APISecret )
		:false;
}

bool ServerCmdQueue::apiSecretDelete(string APIkey){

	return (_apiSecretMgr)
		?_apiSecretMgr->apiSecretDelete(APIkey)
		:false;
}
 
bool ServerCmdQueue::apiSecretGetSecret(string APIkey, string &APISecret){
	return (_apiSecretMgr)
		?_apiSecretMgr->apiSecretGetSecret(APIkey, APISecret)
		:false;
 }

bool ServerCmdQueue::apiSecretMustAuthenticate(){
	return (_apiSecretMgr)
		?_apiSecretMgr->apiSecretMustAuthenticate()
		:false;
 }


