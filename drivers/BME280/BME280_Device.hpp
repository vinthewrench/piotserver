//
//  BME280_Device.hpp
//  pIoTServer
//
//  Created by vinnie on 12/28/24.
//

#ifndef BME280_Device_hpp
#define BME280_Device_hpp

#include <sys/time.h>
#include "pIoTServerDevice.hpp"

#include "BME280.hpp"

// https://www.sparkfun.com/products/15440
// SparkFun Atmospheric Sensor Breakout - BME280


using namespace std;

class BME280_Device : public pIoTServerDevice{
    
public:
    
    static const uint64_t default_queryDelay = 60;
 
    BME280_Device(string devID, string driverName);
    BME280_Device(string devID);
    ~BME280_Device();
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
    
    BME280            _device;
    string            _resultKey_temperature;       //DEGREES_C
    string            _resultKey_humidity;          // RH
    string            _resultKey_pressure;          // HPA
    
    in_state_t         _state;
     timeval            _lastQueryTime;
    uint64_t            _queryDelay;            // how long to wait before next query
    
};
#
#endif /* BME280_Device_hpp */
