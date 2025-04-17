//
//  MCP23008_Device.hpp
//  pIoTServer
//
//  Created by vinnie on 1/4/25.

#ifndef MCP23008_Device_hpp
#define MCP23008_Device_hpp


#include <sys/time.h>
#include <mutex>

#include "pIoTServerDevice.hpp"
#include "MCP23008.hpp"

using namespace std;


class MCP23008_Device : public pIoTServerDevice{
    
public:
    
    typedef enum {
        DIRECTION_INPUT,
        DIRECTION_OUTPUT,
    }direction_t;
 
    typedef struct {
        uint8_t             lineNo;
        direction_t         direction;
   } pin_t;
 
    static const uint64_t default_queryDelay = 60;
  
    MCP23008_Device(string devID, string driverName);
    MCP23008_Device(string devID);
    ~MCP23008_Device();
    bool getVersion(string  &version);
   
    bool initWithSchema(deviceSchemaMap_t deviceSchema);

    bool start();
    void stop();
    
    bool isConnected();
    bool setEnabled(bool enable);
    
    bool getValues( keyValueMap_t &);
    bool setValues(keyValueMap_t kv);
    
    bool allOff();
    

private:
    
    MCP23008                 _device;
    
    map <string, pin_t>     _lines = {};
 
    bool                    _isSetup = false;
    mutable std::mutex      _mutex;
    bool                    _pinDidChange = false;

  };

#endif /* MCP23008_Device_hpp */
