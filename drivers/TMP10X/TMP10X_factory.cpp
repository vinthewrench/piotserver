//
//  TMP10X_factory.c
//  TMP10X
//
//  Created by vinnie on 4/12/25.
//

#include <stdio.h>
#include <string>
#include <iostream>
using namespace std;

#include "TMP10X_Device.hpp"

// the class factory
extern "C" pIoTServerDevice* factory(std::string devID,string driverName) {

    pIoTServerDevice* newPlugin = new TMP10X_Device(devID, driverName);
     return newPlugin;
}
 
