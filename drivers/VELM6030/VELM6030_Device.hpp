//
//  VELM6030_Device.hpp
//  pIoTServer
//
//  Created by vinnie on 1/21/25.
//

#ifndef VELM6030_Device_hpp
#define VELM6030_Device_hpp

#include <sys/time.h>
#include "pIoTServerDevice.hpp"

#include "VELM6030.hpp"

using namespace std;

class VELM6030_Device : public pIoTServerDevice{
    
public:
    
    static const uint64_t default_queryDelay = 60;
    
    VELM6030_Device(string devID, string driverName);
    VELM6030_Device(string devID);
    ~VELM6030_Device();
    bool getVersion(string  &version);

    bool initWithSchema(deviceSchemaMap_t deviceSchema);

    bool start();
    void stop();
    
    bool isConnected();
    bool setEnabled(bool enable);
    
    bool getValues( keyValueMap_t &);
 
   

    void eventNotification(EventTrigger trig);
    

private:
    
     typedef enum  {
        INS_UNKNOWN = 0,
        INS_IDLE ,
        INS_INVALID,
        INS_RESPONSE,  // we DONt use it
     }in_state_t;
    
    VELM6030            _device;
    string              _resultKey_lux;
  
    static constexpr uint64_t fcAvgSummationSeconds = 60*60;
  
    double              _fcAvg;
    bool                _fvAvgAvail = false;
    
    
    bool                _isSetup = false;
    in_state_t          _state;
     timeval            _lastQueryTime;
    uint64_t            _queryDelay;            // how long to wait before next query
    
};


#endif /* VELM6030_Device_hpp */
