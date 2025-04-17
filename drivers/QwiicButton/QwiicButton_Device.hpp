//
//  QwiicButton_Device.hpp
//  pIoTServer
//
//  Created by vinnie on 1/20/25.
//
#ifndef QwiicButton_Device_hpp
#define QwiicButton_Device_hpp
 
#include <sys/time.h>
#include "pIoTServerDevice.hpp"

#include "QwiicButton.hpp"

using namespace std;

class QwiicButton_Device : public pIoTServerDevice{
    
public:
    
    static const uint64_t default_queryDelay = 60;
    
    QwiicButton_Device(string devID, string driverName);
    QwiicButton_Device(string devID);
    ~QwiicButton_Device();
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
    
    typedef enum  {
        INS_UNKNOWN = 0,
        INS_IDLE ,
        INS_INVALID,
        INS_RESPONSE,   
     }in_state_t;
    
    QwiicButton         _device;
    string              _buttonState;
    string              _ledState;

    bool                _isSetup = false;
    in_state_t           _state;
     timeval            _lastQueryTime;
    uint64_t            _queryDelay;            // how long to wait before next query
    
};

#endif /* QwiicButton_Device_hpp */
