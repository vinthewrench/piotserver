//
//  main.cpp
//  pIoTServer
//
//  Created by Vincent Moscaritolo on 2/10/22.
//

// PioT  - raspberry Pi of Things

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>

#include "LogMgr.hpp"

#include "ServerNouns.hpp"
#include "RESTServerConnection.hpp"
#include "pIoTServerMgr.hpp"
#include "pIoTServerAPISecretMgr.hpp"

// MARK: - cmdline options

#ifndef IsNull
#define IsntNull( p )    ( (bool) ( (p) != NULL ) )
#define IsNull( p )      ( (bool) ( (p) == NULL ) )
#endif

int     gVerbose_flag        = 0;
int     gDebug_flag          = 0;
int     gPrint_flag          = 0;

char*   gAssetsDirFilePath   = NULL;
char*   gPropsFileName       = NULL;


/*
 * Shutdown signal state.
 *
 * The signal handler only records the signal. It does not log, call C++ object
 * methods, stop devices, touch the database, join threads, or exit. Those
 * actions are not safe inside a POSIX signal handler.
 */
static volatile sig_atomic_t gShutdownRequested = 0;
static volatile sig_atomic_t gShutdownSignal = 0;


static const char* sSignalName(int signum)
{
    switch(signum) {
        case SIGHUP:
            return "SIGHUP";

        case SIGINT:
            return "SIGINT";

        case SIGQUIT:
            return "SIGQUIT";

        case SIGTERM:
            return "SIGTERM";

        default:
            return "UNKNOWN";
    }
}


static void sSignalHandler(int signum)
{
    gShutdownSignal = signum;
    gShutdownRequested = 1;
}


static void sInstallSignalHandlers()
{
    signal(SIGHUP,  sSignalHandler);
    signal(SIGQUIT, sSignalHandler);
    signal(SIGTERM, sSignalHandler);
    signal(SIGINT,  sSignalHandler);
}


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
    argType_t      type;
    void*          argument;
    const char*    shortName;
    char           charName;
    const char*    longName;
} argTable_t;


static argTable_t sArgTable[] =
{
    /* arguments/modifiers */
    { kArg_Count,    &gVerbose_flag,       "verbose",  'v',  "Enables verbose output" },
    { kArg_Count,    &gDebug_flag,         "debug",    'd',  "Enables debug output" },
    { kArg_Count,    &gPrint_flag,         "print",    'p',  "Print on Terminal" },
    { kArg_String,   &gPropsFileName,      "props",    'f',  " Property File path" },
    { kArg_String,   &gAssetsDirFilePath,  "assets",   ' ',  " Assets Directory path" },
};

#define TableEntries  ((int)(sizeof(sArgTable) / sizeof(argTable_t)))


static void sUsage()
{
    int j;

    printf("\npIoTServer \n\nusage: pIoTServer [options] ..\nOptions: \n ");

    printf("\tOptions:\n");
    for(j = 0; j < TableEntries; j++) {
        if(((sArgTable[j].type == kArg_Boolean)
            || (sArgTable[j].type == kArg_Count)
            || (sArgTable[j].type == kArg_String)
            || (sArgTable[j].type == kArg_HexString)
            || (sArgTable[j].type == kArg_Other))
           && sArgTable[j].longName) {

            printf("\t%s%c   %2s%-10s %s\n",
                   sArgTable[j].charName ? "-" : "",
                   sArgTable[j].charName ? sArgTable[j].charName : ' ',
                   sArgTable[j].shortName ? "--" : "",
                   sArgTable[j].shortName ? sArgTable[j].shortName : "",
                   sArgTable[j].longName);
        }
    }
}


static void sSetupCmdOptions(int argc, const char **argv)
{
    if(argc > 1) {
        for(int i = 1; i < argc; i++) {
            bool found = false;
            size_t temp;

            for(int j = 0; j < TableEntries; j++) {
                if((IsntNull(sArgTable[j].shortName)
                    && ((strncmp(argv[i], "--", 2) == 0)
                        && (strcasecmp(argv[i] + 2, sArgTable[j].shortName) == 0)))
                   || ((*(argv[i]) == '-') && (*(argv[i] + 1) == sArgTable[j].charName))) {

                    found = true;

                    switch(sArgTable[j].type) {
                        case kArg_Boolean:
                            if(IsNull(sArgTable[j].argument)) {
                                continue;
                            }

                            *((bool*)sArgTable[j].argument) = true;
                            break;

                        case kArg_Count:
                            if(IsNull(sArgTable[j].argument)) {
                                continue;
                            }

                            *((bool*)sArgTable[j].argument) = *((bool*)sArgTable[j].argument) + 1;
                            break;

                        case kArg_String:
                            if(IsNull(sArgTable[j].argument)) {
                                continue;
                            }

                            if(IsNull(argv[++i])) {
                                goto error;
                            }

                            temp = strlen(argv[i]);
                            *((char**)sArgTable[j].argument) = (char*)malloc(temp + 2);
                            strcpy(*((char**)sArgTable[j].argument), argv[i]);
                            break;

                        case kArg_HexString:
                            if(IsNull(sArgTable[j].argument)) {
                                continue;
                            }

                            if(IsNull(argv[++i])) {
                                goto error;
                            }

                            temp = strlen(argv[i]);
                            *((char**)sArgTable[j].argument) = (char*)malloc(temp + 2);
                            strcpy(*((char**)sArgTable[j].argument), argv[i]);
                            break;

                        case kArg_UInt:
                            if(IsNull(sArgTable[j].argument)) {
                                continue;
                            }

                            {
                                uint tmp;
                                if(sscanf(argv[++i], "%u", &tmp) == 1) {
                                    *((uint*)sArgTable[j].argument) = tmp;
                                }
                            }
                            break;

                        case kArg_Other:
                        default:
                            break;
                    }

                    break;
                }
            }

            if(!found) {
                goto error;
            }
        }
    }

    return;

error:
    sUsage();
    exit(1);
}


// MARK: - MAIN

int main(int argc, const char * argv[])
{
    int exitCode = EXIT_SUCCESS;

    /*
     * Process command-line options.
     */
    sSetupCmdOptions(argc, argv);

    if(gPrint_flag) {
        START_LOGPRINT;
    }

    if(gVerbose_flag) {
        LogMgr::shared()->_logFlags = LogMgr::LogLevelVerbose;
    }
    else if(gDebug_flag) {
        LogMgr::shared()->_logFlags = LogMgr::LogLevelDebug;
    }

    /*
     * Install signal handlers before startup so we can catch early SIGTERM,
     * SIGINT, SIGQUIT, or SIGHUP.
     */
    sInstallSignalHandlers();

    pIoTServerMgr* pIoTServer = pIoTServerMgr::shared();

    if(IsntNull(gAssetsDirFilePath)) {
        pIoTServer->setAssetDirectoryPath(string(gAssetsDirFilePath));
    }

    if(IsntNull(gPropsFileName)) {
        pIoTServer->setPropFileName(string(gPropsFileName));
    }

    pIoTServer->start();

    /*
     * Set up the API secrets.
     */
    pIoTServerAPISecretMgr apiSecrets(pIoTServer->getDB());

    int restPort = pIoTServer->getDB()->getRESTPort();

    /*
     * Create the server command processor.
     */
    auto cmdQueue = new ServerCmdQueue(&apiSecrets);

    registerServerNouns();

    TCPServer rest_server(cmdQueue);
    rest_server.begin(restPort, true, [=]() {
        return new RESTServerConnection();
    });

    /*
     * Main loop.
     *
     * Shutdown is handled here, not inside the signal handler.
     */
    while(!gShutdownRequested) {
        pIoTServer->setActiveConnections(rest_server.hasActiveConnections());
        sleep(2);
    }

    /*
     * Now we are back in normal program flow. Logging and C++ shutdown are safe.
     */
    LOGT_INFO("main received signal %d (%s), stopping pIoTServer",
              gShutdownSignal,
              sSignalName(gShutdownSignal));

    pIoTServer->stop();

    LOGT_INFO("main exiting cleanly");

    delete cmdQueue;
    cmdQueue = nullptr;

    return exitCode;
}
