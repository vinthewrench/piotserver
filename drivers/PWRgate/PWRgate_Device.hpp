//
//  PWRgate_Device.hpp
//  pIoTServer
//
//  Created by vinnie on 1/9/25.
//

#ifndef PWRgate_Device_hpp
#define PWRgate_Device_hpp


#include <sys/time.h>
#include <termios.h>
#include <unistd.h>
#include <mutex>

#include <thread>            //Needed for std::thread
#include <mutex>

#include "pIoTServerDevice.hpp"

using namespace std;

class PWRgate_Device : public pIoTServerDevice{
    
public:
    
    PWRgate_Device(string devID, string driverName);
    ~PWRgate_Device();
    
    bool getVersion(string  &version);
 
    bool initWithSchema(deviceSchemaMap_t deviceSchema);
    
    bool start();
    void stop();
    
    bool isConnected();
    bool setEnabled(bool enable);
    
    bool hasUpdates();
    bool getValues( keyValueMap_t &);
    
private:
    
    bool openSerialPort(int &error);
    void closeSerialPort();

    
    // mutex protected values
    std::mutex           _mutex;

    struct termios _tty_opts_backup;
    fd_set             _master_fds;     // sockets that are ready for read
    int                _max_fds;
    int                _fd;
 
    bool                _dataDidChange;
    string              _chargeState;
    double              _ps_volts;
    double              _bat_volts;
    double              _charge_current;
    double              _sol_volts;

    // -------------
  
    void                    actionThread();
    std::thread             _thread;                //Internal thread
    bool                    _running;                //Flag for starting and terminating the main loop

    string            _ttyPath;
    speed_t           _ttySpeed;
    string            _resultKey_status;
    string            _resultKey_PWR_volts;
    string            _resultKey_BAT_volts;
    string            _resultKey_BAT_amps;
    string            _resultKey_SOLAR_volts;

    bool               _isSetup = false;
    
  
};

#endif /* PWRgate_Device_hpp */
