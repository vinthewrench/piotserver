//
//  ADS1115_Device.hpp
//  pIoTServer
//
//  Created by vinnie on 4/8/25.
//

#ifndef ADS1115_Device_hpp
#define ADS1115_Device_hpp

#include <sys/time.h>
#include "pIoTServerDevice.hpp"
#include "ADS1115.hpp"

using namespace std;

class ADS1115_Device : public pIoTServerDevice{
    
public:
    
    static const uint64_t default_queryDelay = 60;
    
    ADS1115_Device(string devID, string driverName);
    ADS1115_Device(string devID);
    ~ADS1115_Device();
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
    
  
    ADS1115                _device;

    string               _resultKey;

    bool                _isSetup = false;
    in_state_t          _state;
     timeval            _lastQueryTime;
    uint64_t            _queryDelay;            // how long to wait before next query
  };

#endif /* ADS1115_Device_hpp */
