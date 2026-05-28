#include "VELM6030_Device.hpp"

extern "C" pIoTServerDevice* factory(std::string devID, std::string driverName)
{
    return new VELM6030_Device(devID, driverName);
}
