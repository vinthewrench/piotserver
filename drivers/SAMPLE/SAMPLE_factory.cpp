//
//  SAMPLE_factory.c
//  SAMPLE
//
//  Created by vinnie on 4/12/25.
//

#include <stdio.h>
#include <string>
#include <iostream>
using namespace std;

#include "SAMPLE_Device.hpp"

// the class factory
extern "C" pIoTServerDevice* factory(std::string devID,string driverName) {

    pIoTServerDevice* newPlugin = new SAMPLE_Device(devID, driverName);
     return newPlugin;
}
 
