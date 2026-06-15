//
//  PCA9536_Device.cpp
//  pIoTServer
//
//  Created by vinnie on 1/3/25.
//

#include "PCA9536_Device.hpp"

#include "TimeStamp.hpp"
#include "LogMgr.hpp"
#include "PropValKeys.hpp"
#include "IncidentMgr.hpp"

constexpr string_view Driver_Version = "1.1.0 dev 0";

bool PCA9536_Device::getVersion(string &str){
    str = string(Driver_Version);
   return true;
}

PCA9536_Device::PCA9536_Device(string devID) :PCA9536_Device(devID, string()){};

PCA9536_Device::PCA9536_Device(string devID, string driverName){
    setDeviceID(devID, driverName);
     _isSetup = false;

    json j = {
        { PROP_DEVICE_MFG_URL, {"https://www.ti.com/product/PCA9536#tech-docs",
            "https://store.ncd.io/product/1-channel-signal-relay-1a-spdt-i2c-mini-module/",
            "https://store.ncd.io/product/2-channel-signal-relay-1a-spdt-i2c-mini-module/"}},
         { PROP_DEVICE_MFG_PART, "PCA9536 Remote 4-Bit I2C I/O Expander"},
     };
    setProperties(j);

    _deviceState = DEVICE_STATE_UNKNOWN;

}

PCA9536_Device::~PCA9536_Device(){
    stop();
 }

bool PCA9536_Device::initWithSchema(deviceSchemaMap_t deviceSchema){

    for(const auto& [key, entry] : deviceSchema) {

        _lines[key] = {
            .lineNo  = entry.pinNo,
            .direction = entry.readOnly
            ?DIRECTION_INPUT
            :DIRECTION_OUTPUT
        };
    }

    _isSetup = true;
    _deviceState = DEVICE_STATE_DISCONNECTED;

    return _isSetup;
}

bool PCA9536_Device::start(){
    bool status = false;
    int error = 0;

    if(!_deviceProperties[PROP_ADDRESS].is_string()){
        LOGT_DEBUG("PCA9536_Device begin called with no %s property",string(PROP_ADDRESS).c_str());;
        return false;
    }

    if(_deviceID.size() == 0){
        LOGT_DEBUG("PCA9536_Device has no deviceID");
        return  false;
    }

    string address  = _deviceProperties[PROP_ADDRESS];
    uint8_t i2cAddr = std::stoi(address.c_str(), 0, 16);

    if(!_isSetup){
        LOGT_DEBUG("PCA9536_Device(%s) begin called before initWithKey ",address.c_str());
        return  false;
    }

    LOGT_DEBUG("PCA9536_Device(%02X) begin",i2cAddr);
    if(!_device.begin(i2cAddr, error)){
        LOGT_ERROR("PCA9536_Device begin FAILED: %s",strerror(error));

        IncidentMgr::shared()->raise(
            _deviceID,
            IncidentMgr::Severity::Error,
            "DEVICE_IO_FAILED",
            _deviceID,
            nullptr,
            "PCA9536 begin failed"
        );

        return  false;
    }

    IncidentMgr::shared()->clear(
        _deviceID,
        "DEVICE_IO_FAILED",
        _deviceID,
        nullptr,
        "PCA9536 begin succeeded"
    );

    // calculate setGPIOdirection mask
    uint16_t iomask = 0xFF;
    for(auto line: _lines){
        uint relayNum =  line.second.lineNo ;
        bool dir = line.second.direction;

        if(dir)
            iomask &= ~(1<<(relayNum));
        else
            iomask |= 1<<(relayNum);
    }

    status = _device.setGPIOdirection(iomask);

    if(!status){
        LOGT_DEBUG("PCA9536_Device(%s) setGPIOdirection(%02X) Failed ",address.c_str(), iomask);

        IncidentMgr::shared()->raise(
            _deviceID,
            IncidentMgr::Severity::Error,
            "DEVICE_IO_FAILED",
            _deviceID,
            nullptr,
            "PCA9536 setGPIOdirection failed"
        );

        return  false;
    }

    IncidentMgr::shared()->clear(
        _deviceID,
        "DEVICE_IO_FAILED",
        _deviceID,
        nullptr,
        "PCA9536 setGPIOdirection succeeded"
    );

    if(status){

 //       _device.allOff();

        _pinDidChange = true;
        _deviceState = DEVICE_STATE_CONNECTED;
    }
    else {
        LOGT_ERROR("PCA9536_Device begin FAILED: %s",strerror(errno));

        IncidentMgr::shared()->raise(
            _deviceID,
            IncidentMgr::Severity::Error,
            "DEVICE_IO_FAILED",
            _deviceID,
            nullptr,
            "PCA9536 begin failed"
        );
    }
    return status;
}




void PCA9536_Device::stop(){

    LOGT_DEBUG("PCA9536_Device  stop");

    if(_device.isOpen()){
        if(_device.allOff()){
            IncidentMgr::shared()->clear(
                _deviceID,
                "DEVICE_IO_FAILED",
                _deviceID,
                nullptr,
                "PCA9536 allOff succeeded during stop"
            );
        }
        else {
            IncidentMgr::shared()->raise(
                _deviceID,
                IncidentMgr::Severity::Error,
                "DEVICE_IO_FAILED",
                _deviceID,
                nullptr,
                "PCA9536 allOff failed during stop"
            );
        }

        {
            std::lock_guard<std::mutex> lock(_mutex);
            _pinDidChange = true;
       }
        _device.stop();
    }

    _deviceState = DEVICE_STATE_DISCONNECTED;

 }

bool PCA9536_Device::setEnabled(bool enable){

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


bool PCA9536_Device::allOff(){

    bool status = false;

    if(_device.isOpen()){
        status = _device.allOff();

        if(status){
            IncidentMgr::shared()->clear(
                _deviceID,
                "DEVICE_IO_FAILED",
                _deviceID,
                nullptr,
                "PCA9536 allOff succeeded"
            );
        }
        else {
            IncidentMgr::shared()->raise(
                _deviceID,
                IncidentMgr::Severity::Error,
                "DEVICE_IO_FAILED",
                _deviceID,
                nullptr,
                "PCA9536 allOff failed"
            );
        }
    }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _pinDidChange = true;
    }

    return status;
}

bool PCA9536_Device::isConnected(){

    return _device.isOpen();
}

bool PCA9536_Device::setValues(keyValueMap_t kv){

    if(!isConnected())
        return false;

    PCA9536::pinStates_t ps ;

    for(const auto& [key, valStr] : kv){

        if(_lines.count(key)){
            pin_t pin = _lines[key];
            bool state = false;
            bool isBool = false;

            isBool =  stringToBool(valStr,state);
            if(!isBool) return false;

            ps.push_back( make_pair(pin.lineNo,state));
        }
    }
    if(ps.size()){
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _pinDidChange = true;
        }

        bool status = _device.setRelayStates(ps);

        if(status){
            for(const auto& [pin, state] : ps){
                (void)state;

                for(const auto& [key, line] : _lines){
                    if(pin == line.lineNo){
                        IncidentMgr::shared()->clear(
                            _deviceID,
                            "DEVICE_IO_FAILED",
                            key,
                            nullptr,
                            "PCA9536 setRelayStates succeeded"
                        );
                        break;
                    }
                }
            }
        }
        else {
            for(const auto& [pin, state] : ps){
                (void)state;

                for(const auto& [key, line] : _lines){
                    if(pin == line.lineNo){
                        IncidentMgr::shared()->raise(
                            _deviceID,
                            IncidentMgr::Severity::Error,
                            "DEVICE_IO_FAILED",
                            key,
                            nullptr,
                            "PCA9536 setRelayStates failed"
                        );
                        break;
                    }
                }
            }
        }

        return status;
    }

    return false;
}


bool PCA9536_Device::getValues (keyValueMap_t &results){

    std::lock_guard<std::mutex> lock(_mutex);
     bool hasData = false;
    _pinDidChange = false;

    if(!isConnected()){
        // return zeroed states
        for(auto p : _lines){
                results[p.first] = to_string(0);
            }
        return true;
     }

    PCA9536::pinStates_t ps ;

    if( _device.getGPIOstates(ps)){
        IncidentMgr::shared()->clear(
            _deviceID,
            "DEVICE_IO_FAILED",
            _deviceID,
            nullptr,
            "PCA9536 getGPIOstates succeeded"
        );

        for(const auto& [relay, state] : ps) {
            for(auto p : _lines){
                if(relay == p.second.lineNo){
                    results[p.first] = to_string(state);
                }
            }
        }
        hasData = true;
    }
    else {
        IncidentMgr::shared()->raise(
            _deviceID,
            IncidentMgr::Severity::Error,
            "DEVICE_IO_FAILED",
            _deviceID,
            nullptr,
            "PCA9536 getGPIOstates failed"
        );
    }

    return hasData;
}
