//
//  CPUTEMP_Device.hpp
//  piServer
//
//  Created by vinnie on 12/28/24.
//

#ifndef CPUTEMP_Device_hpp
#define CPUTEMP_Device_hpp

#include <sys/time.h>
#include "PiServerDevice.hpp"


using namespace std;

class CPUTEMP_Device : public PiServerDevice{
    
public:
    
    static const uint64_t default_queryDelay = 5;
    
    CPUTEMP_Device();
    ~CPUTEMP_Device();
    
    bool initWithSchema(deviceSchemaMap_t deviceSchema);

    bool start();
    void stop();
    
    bool isConnected();
    bool hasKey(string key);

    bool getValues( keyValueMap_t &);
 
    bool getDeviceID(string  &devID);

private:
    
    bool getCPUTemp(double & tempOut);
    bool getFanState(uint8_t  &state);

    typedef enum  {
        INS_UNKNOWN = 0,
        INS_IDLE ,
        INS_INVALID,
        INS_RESPONSE,  // we DONt use it
     }in_state_t;
    
    string            _resultKey_temp;
    string            _resultKey_cooler;
    
    bool                _isSetup = false;
    in_state_t         _state;
     timeval            _lastQueryTime;
    uint64_t            _queryDelay;            // how long to wait before next query
    
};

#endif /* CPUTEMP_Device_hpp */
