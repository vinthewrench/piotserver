//
//  TCPServer.cpp

//
//  Created by Vincent Moscaritolo on 3/16/21.
//
 
//General Includes
#include <iostream>
#include <thread>			//Needed for std::thread
#include <cstring>			//Needed for memset and string functions
#include <queue>			//Needed for std::queue
#include <unistd.h>			//Needed for sleep
#include <utility>

//Socket Includes
#include <sys/socket.h>		//Core socket functions and data structures
#include <sys/types.h>		//Core socket functions and data structures
#include <sys/time.h>		//Needed for timeval struct
#include <netdb.h>			//Functions for translating protocol names and host names into numeric addresses
#include <fcntl.h>			//fcntl() function and defines
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>			//Needed for I/O system calls

//Class Headers
#include "TCPServer.hpp"
#include "LogMgr.hpp"

// MARK: - TCPServerMgr

TCPServerMgr *TCPServerMgr::sharedInstance = 0;

TCPServerMgr::TCPServerMgr() {
	_servers.clear();
 };


void TCPServerMgr::registerServer(TCPServer *server){
	_servers.insert(server);
}

void TCPServerMgr::removeServer(TCPServer *server){
	_servers.erase(server);
}

vector<TCPClientInfo> TCPServerMgr::getConnectionList() {
	
	vector<TCPClientInfo> results;
	results.clear();
	
	for( auto s :_servers)
		for( auto con :s->_connections){
			TCPClientInfo info = con->_info;
			results.push_back(info);
		}
	
	return results;
}
 
 
// MARK: - TCPServerConnection
TCPServerConnection::TCPServerConnection( TCPClientInfo::clientType_t clientType,
													  const string clientName)
					:_info(clientType,clientName) {
}

ssize_t TCPServerConnection::sendData(const void *buffer, size_t length){

//	printf("SND %d |%.*s|\n", _fd, (int)length,buffer);
	return ::send(_fd, buffer, length, 0);
}

void  TCPServerConnection::close() {
	
//	printf("CLOSE %d\n", _fd);
	_server->close_socket(_fd);
}
 
void TCPServerConnection::queueRESTCommand( REST_URL url,
														 ServerCmdQueue::cmdCallback_t completion  ){
//#warning DEBUG
//    printf("TCPServerConnection::queueRESTCommand\n");
    _cmdQueue->queueRESTCommand(url, _info, completion);
	
}

bool TCPServerConnection::getAPISecret(string APIkey, string &APISecret){
	return _cmdQueue->apiSecretGetSecret(APIkey, APISecret);
}


bool TCPServerConnection::apiSecretMustAuthenticate(){
	return _cmdQueue->apiSecretMustAuthenticate()	;
}

// MARK: - TCPServer

TCPServer::TCPServer(ServerCmdQueue* cmdQueue) {
	_cmdQueue = cmdQueue;
	_running = false;
	_entryCnt = 0;
	
	_activeConnectionIDs.clear();
	_allowRemote = false;
	_connections.clear();
	
	_timeoutDelay 		= 10;	// time to consider a connection active // mostly for REST
	_lastConnectTime 	= 0;  // time since last connect
}


TCPServer::~TCPServer() {
	_running = false;
	if (_thread.joinable())
		_thread.join();
}

void TCPServer::begin(int portNum,  bool allowRemote, factoryCallback_t factory){

	//Clearing master and read sets
	FD_ZERO(&_master_fds);
	FD_ZERO(&_read_fds);

	_lastConnectTime 	= 0;  // time since last connect

	_port = portNum;
	_factory = factory;
	_running = true;
	_allowRemote = allowRemote;
	_thread = std::thread(&TCPServer::run, this);
	
	TCPServerMgr().shared()->registerServer(this);
	
	
}
/**
 * Stops the server by setting _running flag to false.
 * Waiting for the servers thread to finish its execution.
 */

void TCPServer::stop(){
	_running = false;
	
	TCPServerMgr().shared()->removeServer(this);

 if (_thread.joinable())
 		_thread.join();
}


bool TCPServer::isConnectionActive(uint8_t connID){
	return _activeConnectionIDs.count(connID);
}

bool TCPServer::hasActiveConnections() {

	if(_activeConnectionIDs.size())
		return true;
	
		if(_lastConnectTime == 0)
			return false;

		time_t now =  time(NULL);
		
		if((_lastConnectTime + _timeoutDelay) > now)
			return true;
	
 	return false;
}


#define EXIT_STATUS 1

void TCPServer::run(){

	int max_fd;				//Hold the maximum file descriptor number from the active sockets
	int flags;

	
	// 1.
	_listener_fd = socket_bind();

	// 2.
	if (listen(_listener_fd, BACKLOG) == -1){
		perror("listen");
		exit(EXIT_STATUS);
	}

	// 3.
	// save current flags
	flags = fcntl(_listener_fd, F_GETFL, 0);
	if (flags == -1)
		perror("fcntl");
	// set socket to be non-blocking
	if (fcntl(_listener_fd, F_SETFL, flags | O_NONBLOCK) == -1)
		perror("fcntl");

	// 4.
	//clear master and read sets
	 FD_ZERO(&_master_fds);
	 FD_ZERO(&_read_fds);
	 // add the listener socket descriptor to the master set
	 FD_SET(_listener_fd, &_master_fds);
	 max_fd = _listener_fd;

	// 5.
	while(_running){
		//5.1
		//Set timeout values (for waiting on select())
		struct timeval timeout = {TIMEOUT_SEC, TIMEOUT_USEC};
		//Copy all available (open) sockets to the read set
		_read_fds = _master_fds;
		
		//Select - 	modifies read_fds set to show which sockets are ready for reading
		//			if none are ready, it will timeout after the given timeout values
		int sel = select(max_fd+1, &_read_fds, NULL, NULL, &timeout);
		if (sel == -1) {
			perror("select");
			exit(EXIT_STATUS);
		}
		
		// loop checking for data or connection change
		for (int i=0; i < max_fd+1; i++){
			
			// 5.2
			if (i == _listener_fd){
				max_fd =  check_new_connection(max_fd);
			}
			//5.3
			else {
				//Check if socket is in read set (has data or has closed the connection)
				if (FD_ISSET(i, &_read_fds)) {
					// OK to get data
					
					uint8_t buf[BUFFER_SIZE];
					//Receive data
					ssize_t length = recv(i, buf, sizeof buf, 0);
					if (length <= 0){
						//close socket
					
						auto conn = findConnection(i);
						if(conn){
							
							conn->willClose();
							_activeConnectionIDs.erase(conn->_id);
							
							// remove entry from list
							_connections.remove_if ([conn](const TCPServerConnection* e){
								return e == conn;
							});
							
							delete conn;
						}
						close(i);
						//remove from master set
						FD_CLR(i, &_master_fds);
					}  else {
						// we have bytes in buffer
						
						auto conn = findConnection(i);
						if(conn){
// 							printf("RCV %d %d bytes |%.*s|\n", i, (int)length, (int)length,buf);
							conn->didRecvData(buf, length);
						}
					}
				}
			}
		}
	}
 
  	// 6.
	//Close all socket descriptors, this will terminate all connections
	 for (int i=0; i < max_fd+1; i++){
		//If socket is in the master set it means it is still open - so close it
		if (FD_ISSET(i, &_master_fds))
			close(i);
	 }
}


// MARK:  interrnal socket mamangement code
int TCPServer::socket_bind(){
 
	 int listener = -1;						//Listener socket descriptor
	 
	 struct addrinfo hints;					//Holds wanted settings for the listener socket
	 struct addrinfo *server_info_list;		//A list of possible information to create socket
	 
	 //All the other fields in the addrinfo struct (hints) must contain 0
	 memset(&hints, 0, sizeof hints);
	 
	 //Initialize connection information
	 hints.ai_family =   AF_INET; // AF_UNSPEC;			//Supports IPv4 and IPv6
	 hints.ai_socktype = SOCK_STREAM;		//Reliable Stream (TCP)
	 hints.ai_flags = AI_PASSIVE;			//Assign local host address to socket
	 
	 //Get address information
	 int err;
	err = getaddrinfo(_allowRemote?"0.0.0.0":"localhost",
							 std::to_string(_port).c_str(), &hints, &server_info_list);
	 if (err != 0){
		 std::cerr << "getaddrinfo: " << gai_strerror(err) << std::endl;
		 exit(EXIT_STATUS);
	 }
	 
	
	 //Go over list and try to create socket and bind
	 addrinfo* p;
	 for(p = server_info_list;p != NULL; p = p->ai_next) {
		 
		 /* for setsockopt() SO_REUSEADDR, below */
		 int flags = 1;
		 
		 //Create the socket - system call that returns the file descriptor of the socket
		 listener = socket(p->ai_family, p->ai_socktype,p->ai_protocol);
		 if (listener == -1) {
			 continue; //try next
		 }
		 
		 //Make sure the port is not in use. Allows reuse of local address (and port)
		 
		 flags = 1;
		 if (setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&flags,sizeof(int)) == -1) {
			 perror("setsockopt");
			 exit(EXIT_STATUS);
		 }
		 
		 flags = 1;
		 if (setsockopt(listener, SOL_SOCKET, SO_KEEPALIVE, (void *)&flags, sizeof(flags)))
		 {
			 perror("ERROR: setsocketopt(), SO_KEEPIDLE");
			 exit(EXIT_STATUS);
		 };
		 
		 //Bind socket to specific port (p->ai_addr holds the address and port information)
		 if (::bind(listener, p->ai_addr, p->ai_addrlen) == -1) {
			 close(listener);
			 continue; //try next
		 }
		 
		 break; //success
	 }
	 
	 //No one from the list succeeded - failed to bind
	 if (p == NULL)  {
		 std::cerr << "failed to bind" << std::endl;
		 exit(EXIT_STATUS);
	 }
	 
	 freeaddrinfo(server_info_list);
	 
	return(listener);
}


int TCPServer::check_new_connection(int max_fd){
	
	//Check if listener socket is in read set (has changed and has an incoming connection to accept)
	if (FD_ISSET(_listener_fd,&_read_fds)){
		int client_fd;
		struct sockaddr_storage their_addr;
		socklen_t addr_size = sizeof their_addr;

		//Accept the incoming connection, save the socket descriptor (client_fd)
		client_fd = accept(_listener_fd, (struct sockaddr *)&their_addr, &addr_size);
		if (client_fd == -1){
			perror("accept");
		}
		else{
			//If connection accepted
			
			//Set this socket to be non-blocking
			int flags = fcntl(client_fd, F_GETFL, 0);
			if (flags == -1)
				perror("fcntl");
			
			// set socket to be non-blocking
			if (fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) == -1)
				perror("fcntl");
 
			//Add socket to the master set
			FD_SET(client_fd, &_master_fds);
			//Update max_fd
			if (client_fd > max_fd)
				max_fd = client_fd;
		
			TCPServerConnection* conn = _factory();
	 		conn->_server = this;
			conn->_cmdQueue = _cmdQueue;
			conn->_fd = client_fd;
			conn->_id = _entryCnt++; // every entry id unique
			_activeConnectionIDs.insert(conn->_id);
	
			conn->_info._remoteAddr = their_addr;
			conn->_info._localPort	 = _port;
			conn->_info._connID = conn->_id;
		 
			_connections.push_back(conn);
			
	//		printf("OPEN %d\n", client_fd);
			conn->didOpen();
			_lastConnectTime = time(NULL);
			
		}
	}
	return max_fd;
}


TCPServerConnection* TCPServer::findConnection(int fd){
	for (auto e = _connections.begin(); e != _connections.end(); e++) {
		TCPServerConnection* conn  = *e;
		if((*e)->_fd == fd)
			return conn;
	}
	return NULL;
}

void  TCPServer::close_socket(int fd){
	
	auto conn = findConnection(fd);

	if(conn){
 
		_activeConnectionIDs.erase(conn->_id);

		// remove entry from list
		_connections.remove_if ([conn](const TCPServerConnection* e){
			return e == conn;
		});
 
		close(fd);

		delete conn;

		//remove from master set
		FD_CLR(fd, &_master_fds);
	}

}

