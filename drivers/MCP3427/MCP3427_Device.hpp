//
//  MCP3427_Device.hpp
//  pIoTServer
//
//  Created by vinnie on 12/29/24.
//

#ifndef MCP3427_Device_hpp
#define MCP3427_Device_hpp

#include <sys/time.h>
#include "pIoTServerDevice.hpp"

#include "MCP3427.hpp"

using namespace std;

class MCP3427_Device : public pIoTServerDevice{
    
public:
    
    static const uint64_t default_queryDelay = 60;
    
    MCP3427_Device(string devID, string driverName);
    MCP3427_Device(string devID);
    ~MCP3427_Device();
    bool getVersion(string  &version);
    
    bool initWithSchema(deviceSchemaMap_t deviceSchema);

    bool start();
    void stop();
    
    bool isConnected();
    bool setEnabled(bool enable);
    
    bool getValues( keyValueMap_t &);
 
private:
    
    typedef enum  {
        INS_UNKNOWN = 0,
        INS_IDLE ,
        INS_INVALID,
        INS_RESPONSE,  // we DONt use it
     }in_state_t;
    
    MCP3427            _device;
    string            _resultKey;

    bool                _isSetup = false;
    in_state_t           _state;
     timeval            _lastQueryTime;
    uint64_t            _queryDelay;            // how long to wait before next query
    
};
#
#endif /* MCP3427_Device */
