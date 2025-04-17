//
//  PCA9671_factory.c
//  PCA9671
//
//  Created by vinnie on 4/12/25.
//

#include <stdio.h>
#include <string>
#include <iostream>
using namespace std;

#include "PCA9671_Device.hpp"

// the class factory
extern "C" pIoTServerDevice* factory(std::string devID,string driverName) {

    pIoTServerDevice* newPlugin = new PCA9671_Device(devID, driverName);
     return newPlugin;
}
 
