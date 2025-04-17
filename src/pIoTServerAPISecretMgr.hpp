//
//  pIoTServerAPISecretMgr.hpp
//  pIoTServer
//
//  Created by Vincent Moscaritolo on 9/9/21.
//

#ifndef pIoTServerAPISecretMgr_hpp
#define pIoTServerAPISecretMgr_hpp

#include <stdio.h>

#include "ServerCmdQueue.hpp"
#include "pIoTServerDB.hpp"


class pIoTServerAPISecretMgr : public APISecretMgr {

public:
	pIoTServerAPISecretMgr(pIoTServerDB* db);
	
	virtual bool apiSecretCreate(string APIkey, string APISecret);
	virtual bool apiSecretDelete(string APIkey);
	virtual bool apiSecretGetSecret(string APIkey, string &APISecret);
	virtual bool apiSecretMustAuthenticate();
	
private:
	pIoTServerDB* 	 		_db;

};

#endif /* pIoTServerAPISecretMgr_hpp */
