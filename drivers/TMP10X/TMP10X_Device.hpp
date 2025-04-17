//
//  TMP10X_Device.hpp
//  pIoTServer
//
//  Created by vinnie on 12/28/24.
//

#ifndef TMP10X_Device_hpp
#define TMP10X_Device_hpp

#include <sys/time.h>
#include "pIoTServerDevice.hpp"

#include "TMP10X.hpp"

using namespace std;

class TMP10X_Device : public pIoTServerDevice{
    
public:
    
    static const uint64_t default_queryDelay = 60;
   
    TMP10X_Device(string devID, string driverName);
    TMP10X_Device(string devID);
    ~TMP10X_Device();
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
    
    TMP10X            _device;
    string            _resultKey_temperature;

    bool                _isSetup = false;
    in_state_t          _state;
     timeval            _lastQueryTime;
    uint64_t            _queryDelay;            // how long to wait before next query
    
};

#endif /* TMP10X_Device_hpp */
