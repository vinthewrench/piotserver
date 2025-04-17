//
//  SHT25_Device.hpp
//  pIoTServer
//
//  Created by vinnie on 3/12/25.
//

#ifndef SHT25_Device_hpp
#define SHT25_Device_hpp

#include <sys/time.h>
#include "pIoTServerDevice.hpp"

#include "SHT25.hpp"

using namespace std;

// https://sensirion.com/products/catalog/SHT25-DIS-F
// SHT25-DIS-F ±2% Digital humidity and temperature sensor with filter membrane

class SHT25_Device : public pIoTServerDevice{
    
public:
    
    static const uint64_t default_queryDelay = 60;
    
    SHT25_Device(string devID, string driverName);
    SHT25_Device(string devID);
    ~SHT25_Device();
    bool getVersion(string  &version);

    bool initWithSchema(deviceSchemaMap_t deviceSchema);
    
    bool start();
    void stop();
    
    bool isConnected();
    bool setEnabled(bool enable);
 
    bool getValues( keyValueMap_t &);

private:
    
    bool        _isSetup = false;
    
    typedef enum  {
        INS_UNKNOWN = 0,
        INS_IDLE ,
        INS_INVALID,
        INS_RESPONSE,  // we DONt use it
     }in_state_t;
    
    SHT25            _device;
    string            _resultKey_temperature;       //DEGREES_C
    string            _resultKey_humidity;          // RH
    string            _resultKey_serialNo;          // SERIAL NO
 
    string              _serialNo;
    
    in_state_t         _state;
     timeval            _lastQueryTime;
    uint64_t            _queryDelay;            // how long to wait before next query
    
};
 
#endif /* SHT25_Device_hpp */
 
