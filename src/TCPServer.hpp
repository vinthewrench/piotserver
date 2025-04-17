//
//  TCPServer.hpp

//
//  Created by Vincent Moscaritolo on 3/16/21.
//
 
#ifndef TCPSERVER_H_
#define TCPSERVER_H_

#define BACKLOG 5			//Maximum number of connection waiting to be accepted
#define TIMEOUT_SEC 3		//Timeout parameter for select() - in seconds
#define TIMEOUT_USEC 0		//Timeout parameter for select() - in micro seconds
#define BUFFER_SIZE 512		//Incoming data buffer size (maximum bytes per incoming data)

#include <unistd.h>
#include <sys/time.h>

#include <iostream>
#include <thread>			//Needed for std::thread
#include <cstring>			//Needed for memset and string functions
#include <list>
#include <set>

#include <netinet/in.h>
#include <unordered_set>
#include "ServerCmdQueue.hpp"
#include "TCPClientInfo.hpp"

class TCPServer;
class ServerCmdQueue;
class TCPServerConnection;

class TCPServerMgr {

	friend TCPServerConnection;

public:
	TCPServerMgr();
	
	void registerServer(TCPServer *server);
	void removeServer(TCPServer *server);

	vector<TCPClientInfo> getConnectionList();
	
	static TCPServerMgr *shared() {
			if (!sharedInstance)
				sharedInstance = new TCPServerMgr;
			return sharedInstance;
		}

private:
	static TCPServerMgr *sharedInstance;
	unordered_set<TCPServer*> _servers;
};

class TCPServerConnection { 
	friend TCPServer;
	friend TCPServerMgr;

public:
	virtual void didOpen() {}
	virtual void willClose() {}
	virtual void didRecvData(const void *buffer, size_t length){}

	// for subclass
	virtual void sendString(const std::string) = 0;
	virtual void queueRESTCommand( REST_URL url, ServerCmdQueue::cmdCallback_t completion  );
	virtual bool getAPISecret(string APIkey, string &APISecret);
	virtual bool apiSecretMustAuthenticate();
 
	// useful but not necessary
	virtual void closeConnection() = 0;
	virtual bool isConnected() = 0;
	
	ssize_t 			sendData(const void *buffer, size_t length);
	void 				close();
	
	TCPClientInfo	_info;
	uint8_t 			_id;

protected:
	
	TCPServerConnection(TCPClientInfo::clientType_t clientType,
							  const string clientName );
	
	virtual ~TCPServerConnection() {};
 
	int 						 _fd;
 	TCPServer*				 _server;
	ServerCmdQueue* 			_cmdQueue;
};


/**
 * TCPServer Class Declaration
 * This class accepts incoming TCP connection on a given port and handles incoming data.
 * The main routine (run()) runs in a separate thread to allow the main program to continue its execution.
 * On each incoming data it:
 * 	 Call the callback function with the incoming data (as string)
 */
class TCPServer {
	
	friend TCPServerConnection;
	friend TCPServerMgr;

public:
	typedef std::function<TCPServerConnection*()> factoryCallback_t;

	/**
	 * Constructor.
	 */
	TCPServer(ServerCmdQueue* cmdQueue);

	/**
	 * Destructor.
	 */
	virtual ~TCPServer();
 
	/**
	 * Starts the internal thread that executes the main routine (run()).
	 *
	 * Receives port as integer and a callback function as factoryCallback_t
	 */

	void begin(int portNum, bool localHostOnly, factoryCallback_t);

	/**
	 * Stops the main routine and the internal thread.
	 */
	void stop();

	int getPort() {return _port; };
	
	bool isConnectionActive(uint8_t connID);
	
	bool hasActiveConnections();
	
protected:
	void close_socket(int);

private:
	//Local Variables
	factoryCallback_t	_factory;
	ServerCmdQueue* 		_cmdQueue;
	
	int				 		_port;					//Listener port
	bool					_allowRemote;	

	bool 					_running;				//Flag for starting and terminating the main loop
	std::thread 			_thread;				//Internal thread, this is in order to start and stop the thread from different class methods


	std::list<TCPServerConnection*> _connections;
	
	TCPServerConnection* findConnection(int fd);
	
	/**
	 * This is the main routine of this class.
	 * It accepts incoming connection and receives incoming data from these connections.
	 * It is private because it is only executed in a different thread by begin() method.
	 */
	void run();

	// interrnal socket mamangement code
	int socket_bind();
	int check_new_connection(int max_fd);
	
	uint8_t				_entryCnt;
	set <uint8_t> 		_activeConnectionIDs;

	// used for tracking if we have active connections
	time_t				_lastConnectTime;
	time_t     		_timeoutDelay;

	int _listener_fd;
	fd_set _read_fds;		//Socket descriptor set that holds the sockets that are ready for read
	fd_set _master_fds;		//Socket descriptor set that hold all the available (open) sockets
};


#include <stdexcept>

class TCPServerException: virtual public std::runtime_error {
	 
protected:

	 int error_number;               ///< Error Number
	 
public:

	 /** Constructor (C++ STL string, int, int).
	  *  @param msg The error message
	  *  @param err_num Error number
	  */
	 explicit
	TCPServerException(const std::string& msg, int err_num = 0):
		  std::runtime_error(msg)
		  {
				error_number = err_num;
		  }

	
	 /** Destructor.
	  *  Virtual to allow for subclassing.
	  */
	 virtual ~TCPServerException() throw () {}
	 
	 /** Returns error number.
	  *  @return #error_number
	  */
	 virtual int getErrorNumber() const throw() {
		  return error_number;
	 }
};
 

#endif /* TCPSERVER_H_ */
