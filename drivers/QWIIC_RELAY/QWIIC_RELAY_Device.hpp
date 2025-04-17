//
//  QWIIC_RELAY_Device.hpp
//  pIoTServer
//
//  Created by vinnie on 3/16/25.
//

#ifndef QWIIC_RELAY_Device_hpp
#define QWIIC_RELAY_Device_hpp

#include <sys/time.h>
#include <mutex>
#include "pIoTServerDevice.hpp"
#include "QWIIC_RELAY.hpp"

 
using namespace std;

class QWIIC_RELAY_Device : public pIoTServerDevice{
    
public:
 
    QWIIC_RELAY_Device(string devID, string driverName);
    QWIIC_RELAY_Device(string devID);
    ~QWIIC_RELAY_Device();
    bool getVersion(string  &version);

    bool initWithSchema(deviceSchemaMap_t deviceSchema);
    
    bool start();
    void stop();
    
    bool isConnected();
    bool setEnabled(bool enable);
    
    bool getValues( keyValueMap_t &);
    bool setValues(keyValueMap_t kv);
    bool hasUpdates();
 
    bool allOff();
    
 
private:
    
    bool        _isSetup = false;
    
    QWIIC_RELAY::qwr_model _model =  QWIIC_RELAY::QWR_UNKNOWN;
    QWIIC_RELAY            _device;
    
    map <string, uint8_t>   _pinMap = {};
    
    mutable std::mutex      _mutex;
    bool                    _pinDidChange = false;

  };

#endif /* QWIIC_RELAY_Device_hpp */
