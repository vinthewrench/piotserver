//
//  VALVEMASTER_factory.cpp
//  VALVEMASTER
//
//  Created for pIoTServer plugin loading.
//

#include <string>
#include <iostream>

using namespace std;

#include "VALVEMASTER_Device.hpp"


extern "C" pIoTServerDevice* factory(std::string devID, string driverName)
{
    pIoTServerDevice* newPlugin = new VALVEMASTER_Device(devID, driverName);
    return newPlugin;
}
