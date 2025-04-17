//
//  PCA9536_factory.c
//  PCA9536
//
//  Created by vinnie on 4/12/25.
//

#include <stdio.h>
#include <string>
#include <iostream>
using namespace std;

#include "PCA9536_Device.hpp"

// the class factory
extern "C" pIoTServerDevice* factory(std::string devID,string driverName) {

    pIoTServerDevice* newPlugin = new PCA9536_Device(devID, driverName);
     return newPlugin;
}
 
