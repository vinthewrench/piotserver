//
// SHUTDOWN_SIG.cpp
//

#include <string>

#include "SHUTDOWN_SIG_Device.hpp"

extern "C" pIoTServerDevice* factory(std::string devID, std::string driverName)
{
    return new SHUTDOWN_SIG_Device(devID, driverName);
}
