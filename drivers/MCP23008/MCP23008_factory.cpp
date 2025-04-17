//
//  MCP23008_factory.c
//  MCP23008
//
//  Created by vinnie on 4/12/25.
//

#include <stdio.h>
#include <string>
#include <iostream>
using namespace std;

#include "MCP23008_Device.hpp"

// the class factory
extern "C" pIoTServerDevice* factory(std::string devID,string driverName) {

    pIoTServerDevice* newPlugin = new MCP23008_Device(devID, driverName);
     return newPlugin;
}
 
