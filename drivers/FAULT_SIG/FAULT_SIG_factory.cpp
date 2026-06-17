//
// FAULT_SIG.cpp
//

#include <string>

#include "FAULT_SIG_Device.hpp"

extern "C" pIoTServerDevice* factory(std::string devID, std::string driverName)
{
    return new FAULT_SIG_Device(devID, driverName);
}
