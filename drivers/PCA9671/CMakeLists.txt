

cmake_minimum_required(VERSION 3.0)

#SET (CMAKE_C_COMPILER             "clang")
#SET (CMAKE_CXX_COMPILER            "clang++")

set(LIB_NAME "PCA9671")
project("${LIB_NAME}" VERSION 1.0.1 DESCRIPTION "PCA9671 plugin")


set(CMAKE_CXX_STANDARD 23)
set(CXX_EXTENSIONS ON)

set(MODULE_FILES
	PCA9671_factory.cpp
	PCA9671_Device.cpp
	PCA9671_Device.hpp
	PCA9671.hpp
	PCA9671.cpp
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
COMMAND  mkdir -p ../../plugins && cp libPCA9671.dylib ../../plugins/PCA9671.dylib  )
 elseif()
 add_custom_command(TARGET "${LIB_NAME}" POST_BUILD
COMMAND  mkdir -p ../../plugins && cp libPCA9671.so ../../plugins/PCA9671.so  )
endif()
