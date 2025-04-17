//
//  MCP3427_factory.c
//  MCP3427
//
//  Created by vinnie on 4/12/25.
//

#include <stdio.h>
#include <string>
#include <iostream>
using namespace std;

#include "MCP3427_Device.hpp"

// the class factory
extern "C" pIoTServerDevice* factory(std::string devID, string driverName) {

    pIoTServerDevice* newPlugin = new MCP3427_Device(devID, driverName);
    return newPlugin;
}
 
