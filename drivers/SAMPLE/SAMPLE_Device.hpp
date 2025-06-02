//
//  SAMPLE_Device.hpp
//  pIoTServer
//
//  Created by vinnie on 1/9/25.
//

#ifndef SAMPLE_Device_hpp
#define SAMPLE_Device_hpp


#include <sys/time.h>
#include "pIoTServerDevice.hpp"

using namespace std;

class SAMPLE_Device : public pIoTServerDevice{
    
public:
    
    SAMPLE_Device(string devID, string driverName);
    ~SAMPLE_Device();

    bool initWithSchema(deviceSchemaMap_t deviceSchema);
    
    bool start();
    void stop();
    
    bool isConnected();
    bool setEnabled(bool enable);
    
    bool getValues( keyValueMap_t &);
    
private:
    bool               _isSetup = false;
    time_t             _startup_time;
    string             _resultKey;       //SECONDS
};

#endif /* SAMPLE_Device_hpp */
