#include "TankDepth_Device.hpp"

extern "C" pIoTServerDevice* factory(std::string devID, std::string driverName)

{

    return new TankDepth_Device(devID, driverName);

}
