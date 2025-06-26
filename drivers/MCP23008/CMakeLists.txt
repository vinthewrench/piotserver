

cmake_minimum_required(VERSION 3.0)

#SET (CMAKE_C_COMPILER             "clang")
#SET (CMAKE_CXX_COMPILER            "clang++")

set(LIB_NAME "MCP23008")
project("${LIB_NAME}" VERSION 1.0.1 DESCRIPTION "MCP23008 plugin")


set(CMAKE_CXX_STANDARD 23)
set(CXX_EXTENSIONS ON)

set(MODULE_FILES
	MCP23008_factory.cpp
	MCP23008_Device.cpp
	MCP23008_Device.hpp
	MCP23008.hpp
	MCP23008.cpp
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
target_include_directories("${LIB_NAME}" PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})


if(APPLE)
 add_custom_command(TARGET "${LIB_NAME}" POST_BUILD
COMMAND  mkdir -p ../../plugins && cp libMCP23008.dylib ../../plugins/MCP23008.dylib  )
 elseif()
 add_custom_command(TARGET "${LIB_NAME}" POST_BUILD
COMMAND  mkdir -p ../../plugins && cp libMCP23008.so ../../plugins/MCP23008.so  )
endif()
