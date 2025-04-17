//
//  MCP23008_Device.cpp
//  pIoTServer
//
//  Created by vinnie on 1/3/25.
//

#include "MCP23008_Device.hpp"

#include "TimeStamp.hpp"
#include "LogMgr.hpp"
#include "PropValKeys.hpp"

constexpr string_view Driver_Version = "1.1.0 dev 0";

bool MCP23008_Device::getVersion(string &str){
    str = string(Driver_Version);
    return true;
}

MCP23008_Device::MCP23008_Device(string devID) :MCP23008_Device(devID, string()){};
 
MCP23008_Device::MCP23008_Device(string devID, string driverName){
    setDeviceID(devID, driverName);
    _isSetup = false;

    json j = {
        { PROP_DEVICE_MFG_URL, {"https://www.microchip.com/en-us/product/mcp23008",
            "https://store.ncd.io/product/1-channel-high-power-relay-controller-7-gpio-with-i2c-interface"}
        },
         { PROP_DEVICE_MFG_PART, "8-Bit I2C I/O Expander with Serial Interface"},
     };
    setProperties(j);
  
    _deviceState = DEVICE_STATE_UNKNOWN;

}

MCP23008_Device::~MCP23008_Device(){
    stop();
 }

bool MCP23008_Device::initWithSchema(deviceSchemaMap_t deviceSchema){
    
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

bool MCP23008_Device::start(){
    bool status = false;
    int error = 0;
    
    if(!_deviceProperties[PROP_ADDRESS].is_string()){
        LOGT_DEBUG("MCP23008_Device begin called with no %s property",string(PROP_ADDRESS).c_str());;
        return false;
    }
    
    if(_deviceID.size() == 0){
        LOGT_DEBUG("MCP23008_Device has no deviceID");
        return  false;
    }

    string address  = _deviceProperties[PROP_ADDRESS];
    uint8_t i2cAddr = std::stoi(address.c_str(), 0, 16);
    
    if(!_isSetup){
        LOGT_DEBUG("MCP23008_Device(%s) begin called before initWithKey ",address.c_str());
        return  false;
    }
    
    LOGT_DEBUG("MCP23008_Device(%02X) begin",i2cAddr);
    if(!_device.begin(i2cAddr, error)){
        LOGT_ERROR("MCP23008_Device begin FAILED: %s",strerror(errno));
        return  false;
    }

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
        LOGT_DEBUG("MCP23008_Device(%s) setGPIOdirection(%02X) Failed ",address.c_str(), iomask);
        return  false;
    }
 
    if(status){
        
 //       _device.allOff();
        
        _pinDidChange = true;
        _deviceState = DEVICE_STATE_CONNECTED;
    }
    else {
        LOGT_ERROR("MCP23008_Device begin FAILED: %s",strerror(errno));
    }
    return status;
}
 


  
void MCP23008_Device::stop(){
    
    LOGT_DEBUG("MCP23008_Device  stop");
  
    if(_device.isOpen()){
        _device.allOff();
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _pinDidChange = true;
       }
        _device.stop();
    }
    
    _deviceState = DEVICE_STATE_DISCONNECTED;

 }

bool MCP23008_Device::setEnabled(bool enable){
   
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


bool MCP23008_Device::allOff(){
   
    bool status = false;
 
    if(_device.isOpen()){
        status = _device.allOff();
    }
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _pinDidChange = true;
    }
 
    return status;
}

bool MCP23008_Device::isConnected(){
  
    return _device.isOpen();
}
 
bool MCP23008_Device::setValues(keyValueMap_t kv){
    
    if(!isConnected())
        return false;
    
    MCP23008::pinStates_t ps ;
    
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
  
        return _device.setRelayStates(ps);
    }
    
    return false;
}


bool MCP23008_Device::getValues (keyValueMap_t &results){
    
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
 
    MCP23008::pinStates_t ps ;
    
    if( _device.getGPIOstates(ps)){
        for(const auto& [relay, state] : ps) {
            for(auto p : _lines){
                if(relay == p.second.lineNo){
                    results[p.first] = to_string(state);
                }
            }
        }
        hasData = true;
    }
        
    return hasData;
}


