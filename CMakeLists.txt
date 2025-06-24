
SET (CMAKE_C_COMPILER             "clang")
SET (CMAKE_CXX_COMPILER            "clang++")

# SET (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

cmake_minimum_required(VERSION 3.0)

set(APP_NAME "piotserver")
project("${APP_NAME}")

set(CMAKE_CXX_STANDARD 23)
set(CXX_EXTENSIONS ON)

include_directories(
    "${PROJECT_SOURCE_DIR}/src"
	)
	
file(
    GLOB
    SOURCES
    src/main.cpp
    src/ServerNouns.cpp
    src/DemoSecretMgr.cpp
    src/RPi_RelayBoardDevice.cpp
    src/W1_Device.cpp
    src/VELM6030_Device.cpp
    src/Actuator_Device.cpp
    src/TankDepth_Device.cpp
    src/Sprinkler_Device.cpp

    src/I2C.cpp
    src/ADS1115.cpp
    src/VELM6030.cpp

    src/GPIO.cpp
    src/pIoTServerAPISecretMgr.cpp
    src/pIoTServerDB.cpp
    src/pIoTServerMgr.cpp
    src/pIoTServerEvaluator.cpp
    src/EventTrigger.cpp
    src/Sequence.cpp
    src/Action.cpp
    src/TCA9534.cpp
    src/W1Mgr.cpp
    src/SolarTimeMgr.cpp
    src/sunset.cpp
    src/lunar.cpp
    src/base64.cpp

    src/RESTutils.cpp
    src/ServerCmdQueue.cpp
    src/TCPClientInfo.cpp
    src/TCPServer.cpp
    src/http_parser.c
    src/REST_URL.cpp
    src/RESTServerConnection.cpp
    src/yuarel.c
    src/sha256.cpp
    src/TimeStamp.cpp
    src/LogMgr.cpp
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)
#find_package( CURL REQUIRED )

set(
    PROJECT_LINK_LIBS
    sqlite3
    gpiod
#    CURL::libcurl
    Threads::Threads
)

add_subdirectory("drivers/ADS1115")
add_subdirectory("drivers/BME280")
add_subdirectory("drivers/MCP23008")
add_subdirectory("drivers/MCP3427")
add_subdirectory("drivers/PCA9671")
add_subdirectory("drivers/PCA9536")
add_subdirectory("drivers/QwiicButton")
add_subdirectory("drivers/QWIIC_RELAY")
add_subdirectory("drivers/SHT25")
add_subdirectory("drivers/SHT30")
add_subdirectory("drivers/TCA9534")
add_subdirectory("drivers/TMP10X")
add_subdirectory("drivers/PWRGATE")
add_subdirectory("drivers/SAMPLE")

add_executable("${APP_NAME}" ${SOURCES})
target_link_libraries("${APP_NAME}" ${PROJECT_LINK_LIBS})
