//
//  SHT25_factory.c
//  SHT25
//
//  Created by vinnie on 4/12/25.
//

#include <stdio.h>
#include <string>
#include <iostream>
using namespace std;

#include "SHT25_Device.hpp"

// the class factory
extern "C" pIoTServerDevice* factory(std::string devID,string driverName) {

    pIoTServerDevice* newPlugin = new SHT25_Device(devID, driverName);
    return newPlugin;
}
 
