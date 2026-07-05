//
//  DS2482_factory.cpp
//  DS2482
//
//  pIoTServer plugin factory.
//

#include <stdio.h>
#include <string>
#include <iostream>

using namespace std;

#include "DS2482_Device.hpp"

extern "C" pIoTServerDevice* factory(std::string devID, string driverName)
{
    pIoTServerDevice* newPlugin = new DS2482_Device(devID, driverName);
    return newPlugin;
}
