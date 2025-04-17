//
//  TCA9534_Device.hpp
//  pIoTServer
//
//  Created by vinnie on 1/9/25.
//

#ifndef TCA9534_Device_hpp
#define TCA9534_Device_hpp


#include <sys/time.h>
#include "pIoTServerDevice.hpp"
#include "TCA9534.hpp"

using namespace std;

class TCA9534_Device : public pIoTServerDevice{
    
public:
    
    static const uint64_t default_queryDelay = 60;
    
    TCA9534_Device(string devID, string driverName);
    TCA9534_Device(string devID);
    ~TCA9534_Device();
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
    
    TCA9534                 _device;
    map <string, uint8_t>   _pinMap = {};
    
    bool                _isSetup = false;
};

#endif /* TCA9534_Device_hpp */
