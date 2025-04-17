//
//  SHT30_factory.c
//  SHT30
//
//  Created by vinnie on 4/12/25.
//

#include <stdio.h>
#include <string>
#include <iostream>
using namespace std;

#include "SHT30_Device.hpp"

// the class factory
extern "C" pIoTServerDevice* factory(std::string devID,string driverName) {

    pIoTServerDevice* newPlugin = new SHT30_Device(devID, driverName);
     return newPlugin;
}
 
