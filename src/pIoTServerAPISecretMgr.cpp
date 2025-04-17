//
//  pIoTServerAPISecretMgr.cpp
//  pIoTServer
//
//  Created by Vincent Moscaritolo on 9/9/21.
//

#include "pIoTServerAPISecretMgr.hpp"
#include "LogMgr.hpp"

pIoTServerAPISecretMgr::pIoTServerAPISecretMgr(pIoTServerDB* db){
	_db = db;
}

bool pIoTServerAPISecretMgr::apiSecretCreate(string APIkey, string APISecret){
	return _db->apiSecretCreate(APIkey,APISecret );
}

bool pIoTServerAPISecretMgr::apiSecretDelete(string APIkey){
	return _db->apiSecretDelete(APIkey);
}

bool pIoTServerAPISecretMgr::apiSecretGetSecret(string APIkey, string &APISecret){
	return _db->apiSecretGetSecret(APIkey, APISecret);
}

bool pIoTServerAPISecretMgr::apiSecretMustAuthenticate(){
	return _db->apiSecretMustAuthenticate();
}
