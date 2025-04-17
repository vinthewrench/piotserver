//
//  TCA9534_factory.c
//  TCA9534
//
//  Created by vinnie on 4/12/25.
//

#include <stdio.h>
#include <string>
#include <iostream>
using namespace std;

#include "TCA9534_Device.hpp"

// the class factory
extern "C" pIoTServerDevice* factory(std::string devID,string driverName) {

    pIoTServerDevice* newPlugin = new TCA9534_Device(devID, driverName);
     return newPlugin;
}
 
