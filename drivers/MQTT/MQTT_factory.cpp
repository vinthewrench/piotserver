//
//  MQTT_factory.cpp
//  pIoTServer MQTT plugin factory
//

#include "MQTT_Device.hpp"

#include <string>
#include <utility>

extern "C" pIoTServerDevice* factory(std::string devID,
                                      std::string driverName)
{
    return new MQTT_Device(std::move(devID), std::move(driverName));
}
