//
//  POWERCONTROL_factory.cpp
//  POWERCONTROL
//
//  Created by vinnie on 5/30/26.
//

#include <stdio.h>
#include <string>
#include <iostream>

using namespace std;

#include "POWERCONTROL_Device.hpp"

// the class factory
extern "C" pIoTServerDevice* factory(std::string devID, string driverName)
{
    pIoTServerDevice* newPlugin = new POWERCONTROL_Device(devID, driverName);
    return newPlugin;
}
