//
//  PCA9671_Device.hpp
//  pIoTServer
//
//  Created by vinnie on 1/4/25.

#ifndef PCA9671_Device_hpp
#define PCA9671_Device_hpp


#include <sys/time.h>
#include <mutex>

#include "pIoTServerDevice.hpp"
#include "PCA9671.hpp"

using namespace std;


class PCA9671_Device : public pIoTServerDevice{
    
public:
    
    static const uint64_t default_queryDelay = 60;
 
    PCA9671_Device(string devID, string driverName);
    PCA9671_Device(string devID);
    ~PCA9671_Device();
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
    
    PCA9671                 _device;
    map <string, uint8_t>   _pinMap = {};
 
    bool                    _isSetup = false;
    mutable std::mutex      _mutex;
    bool                    _pinDidChange = false;

  };

#endif /* PCA9671_Device_hpp */
