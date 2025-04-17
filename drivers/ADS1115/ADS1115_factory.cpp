//
//  ADS1115_factory.c
//  ADS1115
//
//  Created by vinnie on 4/12/25.
//

#include <stdio.h>
#include <string>
#include <iostream>
using namespace std;

#include "ADS1115_Device.hpp"

// the class factory
extern "C" pIoTServerDevice* factory(std::string devID,string driverName) {

    pIoTServerDevice* newPlugin = new ADS1115_Device(devID, driverName);
    return newPlugin;
}
 
