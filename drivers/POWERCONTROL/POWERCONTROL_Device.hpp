//
//  POWERCONTROL_Device.hpp
//  pIoTServer
//
//  Created by vinnie on 5/30/26.
//

#ifndef POWERCONTROL_Device_hpp
#define POWERCONTROL_Device_hpp

#include <sys/time.h>

#include "pIoTServerDevice.hpp"
#include "POWERCONTROL.hpp"

using namespace std;

/**
 * @class POWERCONTROL_Device
 *
 * @brief pIoTServer plugin wrapper for the AVR power controller.
 *
 * This first version is intentionally read-only. It publishes AC_OK, status
 * byte, firmware version, and configured wake timer minutes.
 */
class POWERCONTROL_Device : public pIoTServerDevice
{
public:

    static const uint64_t default_queryDelay = 5;

    POWERCONTROL_Device(string devID, string driverName);
    POWERCONTROL_Device(string devID);
    ~POWERCONTROL_Device();

    bool getVersion(string &version);

    bool initWithSchema(deviceSchemaMap_t deviceSchema);

    bool start();
    void stop();

    bool isConnected();
    bool setEnabled(bool enable);

    bool getValues(keyValueMap_t &results);
    bool setValues(keyValueMap_t kv);

private:

    bool        _isSetup = false;

    typedef enum {
        INS_UNKNOWN = 0,
        INS_IDLE,
        INS_INVALID,
    } in_state_t;

    POWERCONTROL    _device;

    string          _resultKey_acOK;          // BOOL
    string          _resultKey_status;        // INT
    string          _resultKey_fwVersion;     // INT
    string          _resultKey_wakeTimerMin;  // INT

    in_state_t      _state;
    timeval         _lastQueryTime;
    uint64_t        _queryDelay;
};

#endif /* POWERCONTROL_Device_hpp */
