# piotserver - Raspberry Pi Internet of Things server 

write some stuff here

# Features

######  I2C Hardware Devices
* [TMP10X -  Low-Power Digital Temperature Sensor](https://www.ti.com/product/TMP102) 
* [SHT25 -  ±1.8% Digital humidity and temperature sensor](https://sensirion.com/products/catalog/SHT25)
* [SHT30 - Digital humidity and temperature sensor with filter membrane](https://sensirion.com/products/catalog/SHT30-DIS-F)
* [BME280 -  Temperature Humidity Pressure Sensor](https://www.bosch-sensortec.com/products/environmental-sensors/humidity-sensors-bme280/)
* [ADS1115 - 16 bit Analog-to-Digital Converter](https://www.ti.com/product/ADS1115)
* [MCP3427 - 2-Channel 16-Bit Analog to Digital Converter](https://ww1.microchip.com/downloads/en/DeviceDoc/22226a.pdf)
* [MCP23008 - 8-Bit I/O Expander](https://www.microchip.com/en-us/product/mcp23008)
* [PCA9536 -  4-Bit I/O Expander](https://www.ti.com/product/PCA9536#tech-docs)
* [PCA9671 - 16-Bit I/O Expander](https://www.nxp.com/products/interfaces/ic-spi-i3c-interface-devices/general-purpose-i-o-gpio/remote-16-bit-i-o-expander-for-fm-plus-ic-bus-with-reset:PCA9671)
* [QwiicButton - SparkFun Qwiic Button](https://www.sparkfun.com/sparkfun-qwiic-button-green-led.html)
* [QWIIC_RELAY - SparkFun Qwiic Relay](https://www.sparkfun.com/sparkfun-qwiic-dual-solid-state-relay.html)
* [TCA9534 - SparkFun Qwiic GPIO](https://www.sparkfun.com/sparkfun-qwiic-gpio.html)
* [Waveshare Raspberry Pi 8-ch Relay Expansion Board](https://www.waveshare.com/catalog/product/view/id/3616/s/rpi-relay-board-b/category/37/)


###### Pseudo Devices 
* Sprinkler Controller 
* Actuator
* Tank Depth Sensor
 
# Articles

pointer to substack

## Status

## Installing and Building on Raspberry Pi

##### Always run the upgrade

```bash
$sudo apt update
$sudo apt-get upgrade
```
##### Install the LLVM toolchain

I chose to use [19.1.4](https://github.com/llvm/llvm-project/releases/) as my last reliable build system. 

```bash
sudo apt-get install clang-19
sudo update-alternatives --install /usr/bin/clang clang /usr/bin/clang-19 100
sudo update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-19 100

sudo update-alternatives --config clang
sudo update-alternatives --config clang++

# check installation
clang --version
clang++ --version
```

##### Install Cmake

I have been using the Cmake system to generate the build files in my projects. [Here are some simple instructions](https://lindevs.com/install-cmake-on-raspberry-pi/) for installing CMake on Raspberry Pi.

```bash
#install cmake
sudo apt install -y cmake

#check the version
cmake --version
```

##### Install Git

As part of the development process, you will need a version of the Git distributed version control system. Even if it’s installed it probably a good idea to update to the latest version

```bash
sudo apt-get install git-core
```

##### Expand the Filesystem
 run the raspi-config and select  Advanced Options / Expand the Filesystem
 then it's a good time to reboot

##### Update the configuration file 

here is a copy of what I am using in my  /boot/firmware/config.txt   file:
``` 
# For more options and information see
# http://rpf.io/configtxt

dtparam=i2c_arm=on

dtoverlay=w1-gpio,gpiopin=4

```

##### A few more libraries 
```bash
sudo apt-get install -y i2c-tools  
sudo apt-get install -y  git-core
sudo apt-get install -y  sqlite3  libsqlite3-dev
```

##### The piotserver App

All this is why we came here.

 ```bash
cd
#clone the repository
git clone https://github.com/vinthewrench/piotserver.git
cd piotserver

#build it
cmake ..
make

# edit the assets/piotserver.props.json file 

# test it
 ./piotserver  -d  -p -f assets
 ```
 
Once you have setup the proper piotserver.props.json script and your devices are working
You can  make it a system service for auto run on boot

copy this into  /etc/init.d/piotserver

 ```bash
#!/bin/sh
### BEGIN INIT INFO

# Provides:piotserver
# Required-Start:$remote_fs $syslog
# Required-Stop:$remote_fs $syslog
# Default-Start:2 3 4 5
# Default-Stop:0 1 6
# Short-Description: piotserver
# Description: piotserver auto start after boot
### END INIT INFO

case "$1" in
    start)
        echo "Starting piotserver"
        cd <yourdirectory>
        nohup  <yourdirectory>/piotserver/piotserver -d  -p -f <yourdirectory>/piotserver/assets &
         ;;
    stop)
        echo "Stopping piotserver"
          killall -9 piotserver
         ;;
    *)
        echo "Usage: service piotserver start|stop"
        exit 1
        ;;
esac
exit 0

 ```

To get the service installed.

 ```bash
sudo chmod +x /etc/init.d/piotserver
sudo update-rc.d piotserver defaults

# to remove it
#sudo update-rc.d  -f piotserver remove
 ```
 
`sudo service piotserver start # start the service`
`sudo service piotserver stop # stop the service`
`sudo service piotserver restart # restart the service`
`sudo service piotserver status # check service status`
 
 
 
 ## Running on docker
 
 create a Dockerfile for the build system
 
 #Dockerfile_cpp
 
```bash
 FROM dtcooper/raspberrypi-os:bookworm
VOLUME "/project1"

WORKDIR "/project1"
 

RUN apt-get update \
   && apt-get -y --no-install-recommends install \
        clang-19 clang++-19  \
        build-essential \
       cmake \
        git-core \
        sqlite3 \
        libsqlite3-dev \
        i2c-tools \
        gpiod \
        libgpiod-dev \
        curl \
    libcurl4-openssl-dev \
        nano \
   && rm -rf /var/lib/apt/lists/*  \
   && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-19 100 \
   && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-19 100
 ```

create a Dockerfile for making the app

#Dockerfile
 
```bash
FROM cpp-build-base:0.1.0 AS build
WORKDIR /work

COPY CMakeLists.txt ./
ADD src ./src
ADD drivers ./drivers
ADD assets ./assets
 
RUN mkdir plugins && cmake . &&  make
#RUN   ls -al /work/plugins

FROM dtcooper/raspberrypi-os:bookworm

RUN apt-get update \
   && apt-get -y --no-install-recommends install \
        git-core \
        libsqlite3-dev \
        gpiod \
        libgpiod-dev \
        libcurl4-openssl-dev \
   && rm -rf /var/lib/apt/lists/*

COPY --from=build /work/piotserver .
RUN  mkdir ./plugins
COPY --from=build /work/plugins  .
RUN  rm *.so
#RUN  ls -l ./plugins

COPY --from=build /work/assets/piotserver.props.json .
ENV TZ="America/Chicago"
 ```
 
 create a docker-compose 
 
 #docker-compose.yml
 ```bash
 name: piotserver
services:
    piotserver:
        container_name: piotserver
        volumes:
            - ~/piotserver/assets:/assets1
            - ~/piotserver/plugins:/plugins
            - /sys/firmware:/sys/firmware
            - /proc:/proc
        security_opt:
            - seccomp:unconfined
        ports:
            - 8081:8081
        devices:
            - /dev/i2c-1
            - /dev/gpiochip0
        environment:
            - TZ=America/Chicago
        stdin_open: false
        tty: true
        image: piotserver:1.1.0
        command: ./piotserver -d -p -f assets1

 ```
 
 Create and start the system on Docker
  ```bash
 
 #create the build system
docker build -f Dockerfile_cpp -t cpp-build-base:0.1.0 .

 #create the container
docker build --no-cache . -t piotserver:1.0.0

#run the container
 docker compose up
 
# as a detached proccess
# docker compose up -d

#To stop the container
# docker compose stop piotserver
 
  ```
  
  
  
