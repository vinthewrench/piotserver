//
//  main.cpp
//  pIoTServer
//
//  Created by Vincent Moscaritolo on 2/10/22.
//

// PioT  - raspberry Pi of Things


#include <iostream>
#include "LogMgr.hpp"

#include "ServerNouns.hpp"
#include "RESTServerConnection.hpp"
#include "pIoTServerMgr.hpp"
#include "pIoTServerAPISecretMgr.hpp"

// MARK: - cmdline options

#ifndef IsNull
#define IsntNull( p )    ( (bool) ( (p) != NULL ) )
#define IsNull( p )        ( (bool) ( (p) == NULL ) )
#endif

int        gVerbose_flag    = 0;
int        gDebug_flag        = 0;
int        gPrint_flag        = 0;

char*      gAssetsFilePath        = NULL;


/* for command line processing */
typedef enum
{
    kArg_Invalid   = 0,
    kArg_Boolean,
    kArg_String,
    kArg_UInt,
    kArg_HexString,
    kArg_Other,
    kArg_Count,
} argType_t;


typedef struct
{
    argType_t         type;
    void*                argument;
    const char*        shortName;
    char                charName;
    const char*        longName;
} argTable_t;
 

static argTable_t sArgTable[] =
{
        
    /* arguments/modifiers */
    { kArg_Count,    &gVerbose_flag ,       "verbose",  'v',    "Enables verbose output" },
    { kArg_Count,    &gDebug_flag    ,      "debug",    'd',    "Enables debug output" },
    { kArg_Count,    &gPrint_flag    ,      "print",    'p',    "Print on Terminal" },
    { kArg_String,   &gAssetsFilePath,        NULL,     'f',    " Assets Directory path" },
  };

#define TableEntries  ((int)(sizeof(sArgTable) /  sizeof(argTable_t)))

static void sUsage()
{
    int j;
    
    printf ("\npIoTServer \n\nusage: pIoTServer [options] ..\nOptions: \n ");
        
    printf("\tOptions:\n" );
    for( j = 0; j < TableEntries; j ++)
        if( ((sArgTable[j].type == kArg_Boolean)
             || (sArgTable[j].type == kArg_Count)
             || (sArgTable[j].type == kArg_String)
             || (sArgTable[j].type == kArg_HexString)
             || (sArgTable[j].type == kArg_Other))
            && sArgTable[j].longName)
            printf("\t%s%c   %2s%-10s %s\n",
                     sArgTable[j].charName?"-":"",  sArgTable[j].charName?sArgTable[j].charName:' ',
                     sArgTable[j].shortName?"--":"",  sArgTable[j].shortName?sArgTable[j].shortName:"",
                     sArgTable[j].longName);
}


static void sSetupCmdOptions (int argc, const char **argv)
{
    
    if(argc > 1)
    {
        for (int i = 1; i < argc; i++)
        {
                 bool found = false;
                size_t    temp;

                for(int  j = 0; j < TableEntries; j ++)
                    if ( (IsntNull( sArgTable[j].shortName)
                         &&  ((strncmp(argv[i], "--", 2) == 0)
                          && (strcasecmp(argv[i] + 2,  sArgTable[j].shortName) == 0)) )
                     || (( *(argv[i]) ==  '-' ) && ( *(argv[i] + 1) == sArgTable[j].charName)))
                    {
                        found = true;
                        switch(sArgTable[j].type)
                        {
                                    
                            case kArg_Boolean:
                                if(IsNull(sArgTable[j].argument)) continue;
                                *((bool*)sArgTable[j].argument) = true;
                                break;

                            case kArg_Count:
                                if(IsNull(sArgTable[j].argument)) continue;
                                *((bool*)sArgTable[j].argument) = *((bool*)sArgTable[j].argument)+1;
                                break;
                                
                            case kArg_String:
                                if(IsNull(sArgTable[j].argument)) continue;
                                if(IsNull(argv[++i]))  goto error;
                                temp = strlen(argv[i]);
                                *((char**)sArgTable[j].argument) = (char*) malloc(temp + 2);
                                strcpy(*((char**)sArgTable[j].argument), argv[i]);
                                break;
                                
                            case kArg_HexString:
                                if(IsNull(sArgTable[j].argument)) continue;
                                if(IsNull(argv[++i]))  goto error;
                                    temp = strlen(argv[i]);
                                *((char**)sArgTable[j].argument) = (char*) malloc(temp + 2);
                                strcpy(*((char**)sArgTable[j].argument), argv[i]);
                                break;
                                
                            case kArg_UInt:
                                if(IsNull(sArgTable[j].argument)) continue;
                            {
                                uint tmp;
                                if( sscanf(argv[++i],"%u",&tmp) == 1)
                                    *((uint*)sArgTable[j].argument) = tmp;
                            }
                                break;
                                
                            case kArg_Other:
                            default:;
                        }
                        break;
                    }
                if(!found) goto error;
            }
        }
    return;
    
error:
    sUsage();
    exit(1);

}

// MARK: - MAIN


int main(int argc, const char * argv[]) {
  
    /* process Test options */
    sSetupCmdOptions(argc, argv);
 
    if(gPrint_flag) START_LOGPRINT;
 
    if(gVerbose_flag) {
        LogMgr::shared()->_logFlags = LogMgr::LogLevelVerbose;
    }else if(gDebug_flag) {
        LogMgr::shared()->_logFlags = LogMgr::LogLevelDebug;
    }
 
    pIoTServerMgr*  pIoTServer = pIoTServerMgr::shared();
    if(IsntNull(gAssetsFilePath))
        pIoTServer->setAssetDirectoryPath(string(gAssetsFilePath));
    
    pIoTServer->start();

    //set up the api secrets
    pIoTServerAPISecretMgr apiSecrets(pIoTServer->getDB());
    
     int restPort =  pIoTServer->getDB()->getRESTPort();
  
    // create the server command processor
    auto cmdQueue = new ServerCmdQueue(&apiSecrets);

	registerServerNouns();

	TCPServer rest_server(cmdQueue);
	rest_server.begin(restPort, true, [=](){
		return new RESTServerConnection();
	});
	
	// run the main loop.
	while(true) {

        pIoTServer->setActiveConnections( rest_server.hasActiveConnections());
         sleep(2);
	}
	
	return 0;
}
