//
//  DS2482_Device.hpp
//  pIoTServer
//
//  pIoTServer device wrapper for DS2482 I2C-to-1Wire bridge.
//
//  First-pass support:
//      - DS18B20 temperature sensors only
//      - One DS2482 I2C bridge
//      - Multiple configured DS18B20 sensors using pins[].address
//

#ifndef DS2482_Device_hpp
#define DS2482_Device_hpp

#include <sys/time.h>

#include <array>
#include <map>
#include <string>
#include <vector>

#include "pIoTServerDevice.hpp"
#include "DS2482.hpp"

using namespace std;

class DS2482_Device : public pIoTServerDevice
{
public:
    static const uint64_t default_queryDelay = 60;

    DS2482_Device(string devID, string driverName);
    DS2482_Device(string devID);
    ~DS2482_Device();

    bool getVersion(string &version);

    bool initWithSchema(deviceSchemaMap_t deviceSchema);

    bool start();
    void stop();

    bool isConnected();
    bool setEnabled(bool enable);

    bool getValues(keyValueMap_t &results);

private:
    typedef enum {
        INS_UNKNOWN = 0,
        INS_IDLE,
        INS_INVALID,
        INS_RESPONSE,
    } in_state_t;

    typedef struct DS18B20Value_t {
        string key;
        string address;
        array<uint8_t, 8> rom = {};
        uint64_t queryDelay = default_queryDelay;
    } DS18B20Value_t;

    bool shouldQuery();
    bool extractSensorAddress(const deviceSchema_t &entry, string &address);
    void raiseValueIncident(const string &key, const string &message);
    void clearValueIncident(const string &key, const string &message);

private:
    DS2482                 _device;
    vector<DS18B20Value_t> _configuredValues;

    bool                   _isSetup = false;
    in_state_t             _state;
    timeval                _lastQueryTime;
    uint64_t               _queryDelay;
};

#endif /* DS2482_Device_hpp */
