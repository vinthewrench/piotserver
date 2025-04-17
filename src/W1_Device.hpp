//
//  W1_Device.hpp
//  pIoTServer
//
//  Created by vinnie on 12/29/24.
//

#ifndef W1_Device_hpp
#define W1_Device_hpp

#include <stdio.h>
 
#include <sys/time.h>
#include "pIoTServerDevice.hpp"


using namespace std;

class W1_Device : public pIoTServerDevice{
    
public:
  
    static bool getW1Devices(std::vector<std::string> &names);
    
    static const uint64_t default_queryDelay = 5;
    
    W1_Device(string devID, string driverName);
    W1_Device(string devID);
    ~W1_Device();
    bool getVersion(string  &version);
    
    bool initWithSchema(deviceSchemaMap_t deviceSchema);

    bool start();
    void stop();
   

    bool isConnected();
    bool setEnabled(bool enable);
    
    bool getValues( keyValueMap_t &);
 
private:
    
     bool processDS18B20(double & tempOut);

    typedef enum  {
        INS_UNKNOWN = 0,
        INS_INIT,
        INS_IDLE ,
        INS_INVALID,
        INS_RESPONSE,  // we DONt use it
     }in_state_t;
    
    string          _pathName;
    bool            _usingOWFS;
    string          _resultKey_temp;

    bool            _isSetup = false;
    in_state_t      _state;
    timeval         _lastQueryTime;
    uint64_t         _queryDelay;            // how long to wait before next query
    
};
 
#endif /* W1_Device_hpp */

