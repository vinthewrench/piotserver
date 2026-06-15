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
 * This driver is intentionally read-only.
 *
 * AVR POWERCONTROL firmware v26 uses a one-byte I2C status protocol:
 *
 *   i2ctransfer -y 1 r1@0x08
 *
 * The returned status byte contains:
 *
 *   bit 1 / 0x02 = red LED logical state, 1 = ON
 *   bit 2 / 0x04 = green LED logical state, 1 = ON
 *   bit 3 / 0x08 = logical AC_OK, 1 = AC OK
 *   bits 4-6 / 0x70 = stored wake preset
 *
 * This plugin publishes AC_OK, raw status byte, optional firmware-version
 * placeholder, and configured wake timer minutes decoded from the status byte.
 *
 * Important:
 * - Normal polling uses only a bare one-byte read.
 * - No register pointer is written.
 * - No repeated-start read is used.
 * - No multi-byte operation is used.
 * - allOff() is explicitly a no-op.
 * - setValues() rejects writes.
 *
 * This plugin must not send relay, shutdown, sleep, reset, LED, wake preset,
 * or power-off commands to the AVR.
 */
class POWERCONTROL_Device : public pIoTServerDevice
{
public:

    static const uint64_t default_queryDelay = 30;

    POWERCONTROL_Device(string devID, string driverName);
    POWERCONTROL_Device(string devID);
    ~POWERCONTROL_Device();

    bool getVersion(string &version);

    bool initWithSchema(deviceSchemaMap_t deviceSchema);

    bool start();
    void stop();

    bool isConnected();
    bool setEnabled(bool enable);

    bool allOff();

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
    string          _resultKey_status;        // INT, raw status byte
    string          _resultKey_wakeTimerMin;  // INT, decoded stored wake preset

    in_state_t      _state;
    timeval         _lastQueryTime;
    uint64_t        _queryDelay;

    bool            _hasLastAcOK;
    bool            _lastAcOK;
};

#endif /* POWERCONTROL_Device_hpp */
