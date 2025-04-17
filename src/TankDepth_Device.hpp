//
//  TankDepth_Device.hpp
//  pIoTServer
//
//  Created by vinnie on 4/8/25.
//

#ifndef TankDepth_Device_hpp
#define TankDepth_Device_hpp

#include <sys/time.h>
#include "pIoTServerDevice.hpp"
#include "ADS1115.hpp"

using namespace std;

class TankDepth_Device : public pIoTServerDevice{
    
public:
    
    constexpr static string_view TANK_EMPTY             = "tank.empty";
    constexpr static string_view TANK_FULL              = "tank.full";
    constexpr static string_view TANK_GALS              = "tank.gals";
    constexpr static uint64_t default_queryDelay = 60;
    
    TankDepth_Device(string devID, string driverName);
    TankDepth_Device(string devID);
    ~TankDepth_Device();
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
    
  
    ADS1115              _device;

    string               _resultKey;

    uint16_t            _tankFull;
    uint16_t            _tankEmpty;
    uint16_t            _tankGals;

    bool                _isSetup = false;
    in_state_t          _state;
    timeval            _lastQueryTime;
    uint64_t            _queryDelay;            // how long to wait before next query
  };

#endif /* TankDepth_Device_hpp */
