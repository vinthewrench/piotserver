

cmake_minimum_required(VERSION 3.0)

#SET (CMAKE_C_COMPILER             "clang")
#SET (CMAKE_CXX_COMPILER            "clang++")
 
set(LIB_NAME "PWRGATE")
project("${LIB_NAME}" VERSION 1.0.1 DESCRIPTION "PWRgate plugin")


set(CMAKE_CXX_STANDARD 23)
set(CXX_EXTENSIONS ON)

set(MODULE_FILES
	PWRgate_factory.cpp
	PWRgate_Device.cpp
	PWRgate_Device.hpp
	../../src/dbuf.cpp
	../../src/dbuf.hpp
	../../src/Utils.hpp
    ../../src/ServerCmdValidators.hpp
    ../../src/pIoTServerDevice.hpp
    ../../src/pIoTServerSchema.hpp
    ../../src/PropValKeys.hpp
    ../../src/json.hpp
     ../../src/CommonDefs.hpp
    )

add_library("${LIB_NAME}" SHARED ${MODULE_FILES})

target_include_directories("${LIB_NAME}" PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
target_include_directories("${LIB_NAME}" PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_custom_command(TARGET "${LIB_NAME}" POST_BUILD 
  COMMAND  mkdir -p ../../plugins && cp libPWRGATE.so ../../plugins/PWRGATE.so  )
  
