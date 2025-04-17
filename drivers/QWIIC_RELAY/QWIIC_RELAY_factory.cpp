//
//  QWIIC_RELAY_factory.c
//  QWIIC_RELAY
//
//  Created by vinnie on 4/12/25.
//

#include <stdio.h>
#include <string>
#include <iostream>
using namespace std;

#include "QWIIC_RELAY_Device.hpp"

// the class factory
extern "C" pIoTServerDevice* factory(std::string devID,string driverName) {

    pIoTServerDevice* newPlugin = new QWIIC_RELAY_Device(devID, driverName);
    return newPlugin;
}
 
