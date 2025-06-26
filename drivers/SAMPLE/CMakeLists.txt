

cmake_minimum_required(VERSION 3.0)

#SET (CMAKE_C_COMPILER             "clang")
#SET (CMAKE_CXX_COMPILER            "clang++")

set(LIB_NAME "SAMPLE")
project("${LIB_NAME}" VERSION 1.0.1 DESCRIPTION "SAMPLE plugin")


set(CMAKE_CXX_STANDARD 23)
set(CXX_EXTENSIONS ON)

set(MODULE_FILES
	SAMPLE_factory.cpp
	SAMPLE_Device.cpp
	SAMPLE_Device.hpp
    ../../src/pIoTServerDevice.hpp
    ../../src/pIoTServerSchema.hpp
    ../../src/PropValKeys.hpp
    ../../src/json.hpp
    ../../src/CommonDefs.hpp
    )

add_library("${LIB_NAME}" SHARED ${MODULE_FILES})

target_include_directories("${LIB_NAME}" PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
target_include_directories("${LIB_NAME}" PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(APPLE)
 add_custom_command(TARGET "${LIB_NAME}" POST_BUILD
COMMAND  mkdir -p ../../plugins && cp libSAMPLE.dylib ../../plugins/SAMPLE.dylib  )
 elseif()
 add_custom_command(TARGET "${LIB_NAME}" POST_BUILD
COMMAND  mkdir -p ../../plugins && cp libSAMPLE.so ../../plugins/SAMPLE.so  )
endif()
