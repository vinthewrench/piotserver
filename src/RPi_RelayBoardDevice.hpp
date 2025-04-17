//
//  RPi_RelayBoardDevice.hpp
//  pIoTServer
//
//  Created by vinnie on 1/3/25.
//

#ifndef RPi_RelayBoardDevice_hpp
#define RPi_RelayBoardDevice_hpp

#include <sys/time.h>
#include <mutex>
#include "pIoTServerDevice.hpp"
#include "GPIO.hpp"

 
using namespace std;

class RPi_RelayBoardDevice : public pIoTServerDevice{
    
public:
 
    RPi_RelayBoardDevice(string devID, string driverName);
    RPi_RelayBoardDevice(string devID);
    ~RPi_RelayBoardDevice();
    bool getVersion(string  &version);
    
    bool initWithSchema(deviceSchemaMap_t deviceSchema);
    
    bool start();
    void stop();
    
    bool isConnected();
    bool setEnabled(bool enable);
   
 
    bool getValues ( keyValueMap_t &);
    bool setValues(keyValueMap_t kv);
 //   bool hasUpdates();
  
    bool allOff();
    
 
private:
    
    bool        _isSetup = false;
    GPIO        _gpio;
    
    map <string, GPIO::gpio_pin_t> _pinMap = {};
  
    mutable std::mutex           _mutex;
    bool                         _pinDidChange = false;
 };

#endif /* RPi_RelayBoardDevice_hpp */
