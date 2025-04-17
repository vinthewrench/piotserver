

cmake_minimum_required(VERSION 3.0)

#SET (CMAKE_C_COMPILER             "clang")
#SET (CMAKE_CXX_COMPILER            "clang++")
 
set(LIB_NAME "SHT30")
project("${LIB_NAME}" VERSION 1.0.1 DESCRIPTION "SHT30 plugin")


set(CMAKE_CXX_STANDARD 23)
set(CXX_EXTENSIONS ON)

set(MODULE_FILES
	SHT30_factory.cpp
	SHT30_Device.cpp
	SHT30_Device.hpp
	SHT30.hpp
	SHT30.cpp
    ../../src/pIoTServerDevice.hpp
    ../../src/I2C.hpp
    ../../src/I2C.cpp
    ../../src/pIoTServerSchema.hpp
    ../../src/PropValKeys.hpp
    ../../src/EventAction.hpp
    ../../src/TimeStamp.hpp
    ../../src/TimeStamp.cpp
    ../../src/LogMgr.hpp
    ../../src/LogMgr.cpp
    ../../src/json.hpp
    ../../src/CommonDefs.hpp
    )

add_library("${LIB_NAME}" SHARED ${MODULE_FILES})

target_include_directories("${LIB_NAME}" PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
target_include_directories("${LIB_NAME}" PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../RESTServer)
target_include_directories("${LIB_NAME}" PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_custom_command(TARGET "${LIB_NAME}" POST_BUILD 
  COMMAND  mkdir -p ../../plugins && cp libSHT30.so ../../plugins/SHT30.so  )
  
