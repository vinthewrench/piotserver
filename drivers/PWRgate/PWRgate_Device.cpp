//
//  PWRgate_Device.cpp
//  pIoTServer
//
//  Created by vinnie on 1/9/25.
//

#include "PWRgate_Device.hpp"
#include "PropValKeys.hpp"
#include "ServerCmdValidators.hpp"
#include "LogMgr.hpp"

#include <fcntl.h>
#include <cassert>
#include <string.h>
#include <stdlib.h>
#include <errno.h> // Error integer and strerror() function
#include <termios.h>
#include <unistd.h>
#include <sys/time.h>

#include <thread>            //Needed for std::thread
#include <mutex>
#include "dbuf.hpp"
#include "Utils.hpp"



// MARK: -  SERIAL I/O
/* add a fd to fd_set, and update max_fd */
static int safe_fd_set(int fd, fd_set* fds, int* max_fd) {
     assert(max_fd != NULL);

     FD_SET(fd, fds);
     if (fd > *max_fd) {
          *max_fd = fd;
     }
     return 0;
}

/* clear fd from fds, update max fd if needed */
static int safe_fd_clr(int fd, fd_set* fds, int* max_fd) {
     assert(max_fd != NULL);

     FD_CLR(fd, fds);
     if (fd == *max_fd) {
          (*max_fd)--;
     }
     return 0;
}


bool PWRgate_Device::openSerialPort(int &error){
    
    std::lock_guard<std::mutex> lock(_mutex);
    
    if(_ttyPath.empty()  || _ttySpeed == B0) {
        error = EINVAL;
        return false;
    }
    
    struct termios options;
    int fd ;
    
    if((fd = ::open( _ttyPath.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_NDELAY  )) <0) {
        
        printf("OPEN %s Error: %d %s\n", _ttyPath.c_str(), errno, strerror(errno) );
        error = errno;
        return false;
    }
    
    fcntl(fd, F_SETFL, 0);      // Clear the file status flags
    
    // Back up current TTY settings
    if( tcgetattr(fd, &_tty_opts_backup)<0) {
        LOGT_ERROR("tcgetattr() %s Error %d, %s",  _ttyPath.c_str(),
                   errno, strerror(errno));
        error = errno;
        return false;
    }
    
    cfmakeraw(&options);
    options.c_cflag &= ~PARENB; // Clear parity bit, disabling parity (most common)
    options.c_cflag &= ~CSTOPB; // Clear stop field, only one stop bit used in communication (most common)
    options.c_cflag &= ~CSIZE; // Clear all bits that set the data size
    options.c_cflag |= CS8; // 8 bits per byte (most common)
    options.c_cflag &= ~CRTSCTS;            // Disable hardware flow control
    options.c_cflag |= (CREAD | CLOCAL); // Turn on READ & ignore ctrl lines (CLOCAL = 1)
    
    options.c_lflag &= ~ICANON;
    options.c_lflag &= ~ECHO; // Disable echo
    options.c_lflag &= ~ECHOE; // Disable erasure
    options.c_lflag &= ~ECHONL; // Disable new-line echo
    options.c_lflag &= ~ISIG; // Disable interpretation of INTR, QUIT and SUSP
    options.c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl
    options.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL); // Disable any special handling of received bytes
    
    options.c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
    options.c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed
    
    cfsetospeed (&options, _ttySpeed);
    cfsetispeed (&options, _ttySpeed);
    
    if (tcsetattr(fd, TCSANOW, &options) < 0){
        LOGT_ERROR("tcsetattr() %s Error %d, %s",  _ttyPath.c_str(),
                   errno, strerror(errno));
        error = errno;
        return false;
    }
    
    _fd = fd;
    // add to read set
    safe_fd_set(_fd, &_master_fds, &_max_fds);
    
    return true;
}

void PWRgate_Device::closeSerialPort(){
  
   if(isConnected()){
       std::lock_guard<std::mutex> lock(_mutex);

       // Restore previous TTY settings
       tcsetattr(_fd, TCSANOW, &_tty_opts_backup);
       close(_fd);
       safe_fd_clr(_fd, &_master_fds, &_max_fds);
       _fd = -1;
 
       _chargeState        = "INVALID";
       _ps_volts           = 0;
       _bat_volts          = 0;
       _charge_current     = 0;
       _sol_volts          = 0;
       _dataDidChange = true;
    }
}


// MARK: - PowerGate

constexpr string_view Driver_Version = "1.1.0 dev 0";

bool PWRgate_Device::getVersion(string &str){
    str = string(Driver_Version);
    return true;
}

PWRgate_Device::PWRgate_Device(string devID, string driverName){

    setDeviceID(devID, driverName);
 
    _deviceState = DEVICE_STATE_UNKNOWN;
    _isSetup = false;
    _max_fds = 0;
    _ttyPath.clear();
    _ttySpeed = B0;
    _fd = -1;

    _chargeState        = "INVALID";
    _ps_volts           = 0;
    _bat_volts          = 0;
    _charge_current     = 0;
    _sol_volts          = 0;
    _dataDidChange      = false;
    
       json j = {
             { PROP_DEVICE_MFG_URL, "https://powerwerx.com/west-mountain-radio-epic-pwrgate"},
              { PROP_DEVICE_MFG_PART, "West Mountain Radio Epic PWRgate 12V Backup Power System"},
       };
    
    setProperties(j);
}

PWRgate_Device::~PWRgate_Device(){
    stop();
}


/*
 "bit": 1,
  "title": "PWRGate STATUS"
},
{
 "bit": 2,
 "data_type": "VOLTS",
 "key": "POWER_SUPPLY",
 "title": "Power Supply"
},
{
 "bit": 3,
 "data_type": "VOLTS",
 "key": "BATTERY_VOLTS",
 "title": "Battery Volts"
},
{
 "bit": 4,
 "data_type": "AMPS",
 "key": "CHARGE_AMPS",
 "title": "Charge Current"
},
{
 "bit": 5,
 "data_type": "VOLTS",
 "key": "SOLAR_VOLTS",
 "title": "Solar Volts"
}
 
  
 
 */
bool PWRgate_Device::initWithSchema(deviceSchemaMap_t deviceSchema){
    bool found = false;
    
    for(const auto& [key, entry] : deviceSchema) {
        
        switch(entry.pinNo){
            case 1:
                //   PWRGate Status String
                if(entry.units == STRING ){
                    _resultKey_status = key;
                    found = true;
                }
                break;
                
            case 2:
                //   Power Supply Volts
                if(entry.units == VOLTS ){
                    _resultKey_PWR_volts = key;
                    found = true;
                }
                break;
                
            case 3:
                //   Battery Volts
                if(entry.units == VOLTS ){
                    _resultKey_BAT_volts = key;
                    found = true;
                }
                break;
                
            case 4:
                //   Charge Current
                if(entry.units == AMPS ){
                    _resultKey_BAT_amps = key;
                    found = true;
                }
                break;
                
            case 5:
                //   Solar Volts
                if(entry.units == VOLTS ){
                    _resultKey_SOLAR_volts = key;
                    found = true;
                }
                break;
                
            default:
                break;
        }
    }
    if(found){
        _isSetup = true;
        return true;
    }
    
    
    _deviceState = DEVICE_STATE_DISCONNECTED;
    return false;
}



bool PWRgate_Device::start(){
    
    _ttySpeed = B9600;
 
    if( _deviceProperties.contains(PROP_DEVICE_PARAMS)
       && _deviceProperties[PROP_DEVICE_PARAMS].is_object()){
        json params = _deviceProperties[PROP_DEVICE_PARAMS];
        
        if(params.contains("tty")
           && params["tty"].is_string())
            _ttyPath = params["tty"];
        
        unsigned long speed = 0;
        if( params.contains("speed")){
            if(JSON_value_toUnsigned(params["speed"], speed)){
                _ttySpeed = speed;
            }
        }
    }

#if defined(__APPLE__)
    _ttyPath =  "/dev/tty.usbmodem14201";
    //       _ttyPath =  /dev/tty.Bluetooth-Incoming-Port";
#else
    if(_ttyPath.empty()){
         _ttyPath =  "/dev/ttyACM0";
     }
#endif

    LOGT_DEBUG("PowerGate ready") ;
    
    _running = true;
    _thread = std::thread(&PWRgate_Device::actionThread, this);
  
    _deviceState = DEVICE_STATE_CONNECTED;
    return true;
}




void PWRgate_Device::stop(){
    
    LOGT_DEBUG("PowerGate stop");

    _deviceState = DEVICE_STATE_DISCONNECTED;
    
    _running = false;
   
    // wait for action thread to complete
    while(!_thread.joinable()){
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
     }
    _thread.join();

    
}

bool PWRgate_Device::setEnabled(bool enable){
    
    if(enable){
        _isEnabled = true;
        
        if( _deviceState == DEVICE_STATE_CONNECTED){
            return true;
        }
        
        // force restart
        stop();
        
        bool success = start();
        return success;
    }
    
    _isEnabled = false;
    if(_deviceState == DEVICE_STATE_CONNECTED){
        stop();
    }
    return true;
}


bool PWRgate_Device::isConnected(){
    bool val = false;

    {
        std::lock_guard<std::mutex> lock(_mutex);
        val = _fd != -1;
      }
   
    return val;
}


bool PWRgate_Device::hasUpdates(){
    std::lock_guard<std::mutex> lock(_mutex);
   
    return _dataDidChange;
}


bool PWRgate_Device::getValues (keyValueMap_t &results){
    
    if(!isConnected())
        return false;
     {
        // COPY DATA WITH MUTEX
        std::lock_guard<std::mutex> lock(_mutex);
 
        results[_resultKey_status] = _chargeState;
        results[_resultKey_PWR_volts] = to_string(_ps_volts);
        results[_resultKey_BAT_volts] = to_string(_bat_volts);
        results[_resultKey_BAT_amps] = to_string(_charge_current);
        results[_resultKey_SOLAR_volts] = to_string(_sol_volts);
        _dataDidChange = false;
    }
 
    
     return true;
}


static bool processPGString(uint8_t *data, size_t len,
                                string &chargeState,
                                double &ps_volts,
                                double &bat_volts,
                                double &charge_current,
                                double &sol_volts
                                ){
  
    /*
     Charging  PS=14.09V Bat=13.18V,  0.65A  Sol= 0.08V   Min=0
     Trickle   PS=14.09V Bat=13.57V,  0.05A  Sol= 0.08V   Min=38
     No Bat    PS=14.15V Bat= 0.01V,  0.00A  Sol= 0.04V   Min=0
     PS Off    PS= 8.78V Bat=13.35V,  0.00A  Sol= 0.08V   Min=0
     MPPT-T   PS= 0.00V Bat=13.47V,  0.17A  Sol=14.13V   Min=1/4
     MPPT-T   PS=14.11V Bat=13.37V,  0.00A  Sol= 0.08V   Min=32/35
     Suspend   PS=14.11V Bat=13.39V,  0.00A  Sol= 0.12V   Min=0/35
     Chrg Off  PS=14.11V Bat=13.39V,  0.00A  Sol= 0.12V   Min=0
     PS Off    PS= 0.00V Bat=13.35V,  0.00A  Sol= 0.08V   Min=0/2

     status  PS=xx.xxV Bat=xx.xxV, xx.xxA Sol=xx.xxV  Min=xx  Temp=xx ...
                                                      Min=xx/yy

     status may be one of:
     Testing
     PS Off
     Chrg Off
     No Bat
     Bad Bat x
     Charged
     Charging
     Trickle
     Suspend
     Bad temp
     MPPT
     MPPT-T

     Min=xx is the number of minutes in the current state
     Min=xx/yy adds the number of minutes since the last full recharge
 
     ... Diagnostic data might appear at the end of the line if a P G or B key was pressed.
         Also any line that starts with "Target" does not follow the above format and is diagnostic data
  
   Trickle   PS=14.11V Bat=13.61V,  0.00A  Sol= 0.08V   Min=5  P=399 adc=1
   TargetV=13.55V  TargetI= 1.00A   Stop= 0.25A  Temp=90  PSS=0

     */
    
    char state[10];
    double ps, bat, batA, sol;
    
    // if the line begins with Target, It's diag data.. reject it
    
    if(strncmp((char*)data, "Target", 6) == 0)
        return false;
    
    int n = sscanf((char*)data,
                   "%9c PS=%lfV Bat=%lfV,%lfA Sol=%lfV",
                   state, &ps, &bat, &batA, &sol);

    if(n == 5){
        chargeState = string(state);
        ps_volts = ps;
        bat_volts = bat;
        charge_current = batA;
        sol_volts = sol;
        return true;
    }
   
     return false;
}

void PWRgate_Device::actionThread(){
    
    typedef enum  {
        STATE_INIT = 0,
        STATE_SYNCING,      // looking for first non whitespace
        STATE_READING,
        STATE_DATA,
        STATE_ERROR
    }pg_state_t;
    
    dbuf   buff;
    pg_state_t pg_state = STATE_INIT;
    buff.reset();
    
    while(_running){
        
        // if not setup // check back later
        if(!_isSetup){
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        
        int lastError = 0;
        
        // is the port setup yet?
        if (! isConnected()){
            if(!openSerialPort(lastError)){
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
            buff.reset();
        }
   
        /* wait for something to happen on the socket */
        
        // we use a timeout so we can end this thread when _isSetup is false
        struct timeval selTimeout;
        selTimeout.tv_sec = 2;       /* timeout (secs.) */
        selTimeout.tv_usec = 0;            /* 200000 microseconds */
        
        /* back up master */
        fd_set dup = _master_fds;
        
        int numReady = select(_max_fds+1, &dup, NULL, NULL, &selTimeout);
        if( numReady == -1 ) {
            LOGT_ERROR("Serial port %s select() Error: %d %s", _ttyPath.c_str(),
                       errno, strerror(errno));
            pg_state = STATE_ERROR;
            break;
        }
        else if(numReady == 0){
            // get it out of interactive mode
            for(int i = 0; i < 12; i++) {
                write(_fd, "\r",1);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
             continue;
        }
        else  if ((_fd != -1)  && FD_ISSET(_fd, &dup)) {
            
            u_int8_t c;
            size_t nbytes =  (size_t)::read( _fd, &c, 1 );
            
            if(nbytes == 1){

                switch (pg_state) {
                        
                    case  STATE_INIT:
                        if(c == '\n'){
                            buff.reset();
                            pg_state = STATE_SYNCING;
                        }
                        break;
                        
                    case STATE_SYNCING:
                        if(!isspace(c))
                        {
                            pg_state = STATE_READING;
                            buff.reset();
                            buff.append_char(c);
                        }
                        break;
                        
                    case STATE_READING:
                    {
                        string  chargeState;
                        double  ps_volts;
                        double  bat_volts;
                        double  charge_current;
                        double  sol_volts;
                        
                        if(c ==  '\r') {
                            buff.append_char(0);
                            
                            if(  processPGString(buff.data(), buff.size(),
                                                 chargeState,
                                                 ps_volts,
                                                 bat_volts,
                                                 charge_current,
                                                 sol_volts)) {
                                
                                std::lock_guard<std::mutex> lock(_mutex);
                                // COPY DATA WITH MUTEX
     
                                
                                if(_chargeState         != chargeState
                                   || _ps_volts         != ps_volts
                                   || _bat_volts        != bat_volts
                                   || _charge_current   != charge_current
                                   || _sol_volts        != sol_volts){
                                    
                                    _chargeState        = chargeState;
                                    _ps_volts           = ps_volts;
                                    _bat_volts          = bat_volts;
                                    _charge_current     = charge_current;
                                    _sol_volts          = sol_volts ;
    
                                   cout << "state: " << chargeState
                                   << " PS: " << ps_volts << "V"
                                   << " BAT: " <<  bat_volts << "V"
                                   << " @ " <<  charge_current << "A"
                                   << " SOL: " <<  sol_volts << "V"
                                   << endl;

                                    _dataDidChange = true;
                                }
              
                              }
                             
                            buff.reset();
                            pg_state = STATE_INIT;
                        }
                        else
                        {
                            buff.append_char(c);
                        }
                    }
                        break;
                        
                    default:
                        break;
                }
              }
            else if( nbytes == 0) {
                continue;
            }
            else if( nbytes == -1) {
                int lastError = errno;
                
                // no data try later
                if(lastError == EAGAIN)
                    continue;
                
                if(lastError == ENXIO){  // device disconnected..
                    pg_state = STATE_ERROR;
                    LOGT_ERROR("Serial port %s  disconnected", _ttyPath.c_str());
                    closeSerialPort();
                }
                else {
                    LOGT_ERROR("Serial port %s read() Error %d %s", _ttyPath.c_str(),
                               lastError, strerror(lastError));
                    closeSerialPort();
                }
            }
        }
    }
    
    closeSerialPort();
}




