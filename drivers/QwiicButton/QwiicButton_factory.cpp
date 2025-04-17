//
//  QwiicButton_factory.c
//  QwiicButton
//
//  Created by vinnie on 4/12/25.
//

#include <stdio.h>
#include <string>
#include <iostream>
using namespace std;

#include "QwiicButton_Device.hpp"

// the class factory
extern "C" pIoTServerDevice* factory(std::string devID,string driverName) {

    pIoTServerDevice* newPlugin = new QwiicButton_Device(devID, driverName);
      return newPlugin;
}
 
