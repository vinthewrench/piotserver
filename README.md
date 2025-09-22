# piotserver - Raspberry Pi Internet of Things server 

# Articles

Start with these two
* [Off-Grid Farm Automation with Raspberry Pi](https://www.vinthewrench.com/p/off-grid-farm-automation-raspberry-pi) 
* [Farm automation made easy as Pi](https://www.vinthewrench.com/p/raspberry-pi-internet-of-things-part)

And these are the details
* [Controlling the Irrigation valves.](https://www.vinthewrench.com/p/raspberry-pi-internet-of-things-part-23c)
* [Reading the Temperature with 1-wire Sensors](https://www.vinthewrench.com/p/raspberry-pi-internet-of-things-part-a4c)
* [Using the I2C protocol to communicate to sensors.](https://www.vinthewrench.com/p/raspberry-pi-internet-of-things-part-688)
* [Reading Environmental Data with I2C](https://www.vinthewrench.com/p/raspberry-pi-internet-of-things-part-a03)
* [Rain Sensors](https://www.vinthewrench.com/p/raspberry-pi-internet-of-things-part-cfd)
* [More I/O](https://www.vinthewrench.com/p/raspberry-pi-internet-of-things-part-61b)
* [Getting the data and controlling the devices from the REST API](https://www.vinthewrench.com/p/raspberry-pi-internet-of-things-part-8e7)
* [Writing your own device plug-ins](https://www.vinthewrench.com/p/raspberry-pi-internet-of-things-part-9ef)
* [Using software devices to build on the hardware.](https://www.vinthewrench.com/p/raspberry-pi-internet-of-things-part-094)
* [How to make things happen automatically](https://www.vinthewrench.com/p/raspberry-pi-internet-of-things-part-12f)
* [How to make things happen on time](https://www.vinthewrench.com/p/raspberry-pi-internet-of-things-part-eb8)
* [There are always conditions to evaluate](https://www.vinthewrench.com/p/raspberry-pi-internet-of-things-part-3aa)
* [Rolling my own I²C and 1-Wire interface card](https://www.vinthewrench.com/p/raspberry-pi-internet-of-things-part-2a8)
* [PWM, a better way to control sprinkler valves](https://www.vinthewrench.com/p/raspberry-pi-internet-of-things-part-0d5)
* [Not Another Sprinkler Valve Article; TLDR](https://www.vinthewrench.com/p/not-another-sprinkler-valve-article)

 
 
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
 
 #### Hardware
 * [nodeLynk I²C](https://ncd.io/blog/nodelynk-i2c/)
 * [SparkFun Qwiic](https://www.sparkfun.com/qwiic)
 * [adafruit I²C](https://www.adafruit.com/category/613)
 * [Iowa Scaled Engineering](https://www.iascaled.com/store/)
 * [Waveshare](https://www.waveshare.com/product/raspberry-pi/hats.htm)
 

## Status

## Build system and hardware

Runs on Raspberry Pi (Bookworm)

tested on 
*  Raspberry Pi Zero 2 W Rev 1.0
*  Raspberry Pi 5 Model B Rev 1.0
*  Docker version 27.4.0, build bde2b89

Why is there an Xcode file?
 - I do debugging on Xcode / should probably move to VSCode

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
sudo apt-get install gpiod libgpiod-dev
```

##### this is what my  /boot/firmware/config.txt  looks like

``` 
## For more options and information see
# http://rptl.io/configtxt
# Some settings may impact device functionality. See link above for details

# Uncomment some or all of these to enable the optional hardware interfaces
dtparam=i2c_arm=on

dtparam=audio=off
dtoverlay=disable-bt
camera_auto_detect=0
display_auto_detect=0
hdmi_ignore_hotplug=1
hdmi_blanking=2

;POWER_DOWN - OUTPUT
dtoverlay=gpio-poweroff,gpiopin=25

;BAT_LOW - INPUT
dtoverlay=gpio-shutdown,gpio_pin=23,active_low=1,gpio_pull=up

# Automatically load initramfs files, if found
auto_initramfs=1

# Run in 64-bit mode
arm_64bit=1

# Run as fast as firmware / board allows
arm_boost=1

enable_uart=1
##### The piotserver App
```

be sure to reboot after making these changes.

#### check power supply / battery 

* **POWER_DOWN** - gpiopin=25 is connected to the relay that shuts off the board.
you can test this by doing a shutdown and seeing if the board shuts off.
  
 ```bash
sudo shutdown now
 ```
 
 * **BAT_LOW**  - ,gpio_pin=23 is connected to the DRC battery low signal and 
 will initiate a shutdown on the Raspberry pi
 

* GPIO pin 24 is connected to AC OK on the DRC power supply, you can check this by
 ```bash
sudo pinctrl  set 24 ip pu

# with AC applied this should return 0
# When AC is unplugged, it will return 1
 pinctrl lev 24 
 ```

#### The Hardware UART
**Note: ** I added the **enable_uart** as a backup for when wifi is dead and I cant ssh 

If you pan to use this, you might need to setup a login password.
 ```bash
 #change or setup your password
 $passwd
 
 you can check it by looking at the password file
 $cat /etc/passwd
 
 #also note the console speed should be 115200 -- 
 
$cat  /boot/firmware/cmdline.txt
console=serial0,115200 console=tty1   .....
##### WIFI power saving

 I am disabling WIFI power saving, maybe Raspberry Pi fixed this by now
 ```bash
 sudo nano /etc/NetworkManager/conf.d/wifi-powersave-off.conf
 
 # add in this line
[connection]
wifi.powersave = 2

#after reboot , check it

$ iw dev wlan0 get power_save
Power save: off
 ```
 
#### Setting up the 1-Wire ds2482 OWFS  driver

Check if your hardware is working.  you should at the very least see the DS2482 at address 0x18
 ```bash
sudo i2cdetect -y 1

     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:                         -- -- -- -- -- -- -- --
10: -- -- -- -- -- -- -- -- 18 -- -- -- -- -- -- --
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
60: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
70: -- -- -- -- -- -- -- --
 ```
 
Install OWFS
```bash
sudo apt-get update
sudo apt-get install owfs ow-shell
```
Edit owfs.conf to enable the I2C 1 Wire interface
```bash
sudo nano /etc/owfs.conf

#comment out everything but make sure it reads like
# you dont need the http or ftp ports  
#although the http might be useful for debugging your 1 wire devices

server: device = /dev/i2c-1
mountpoint = /mnt/1wire
allow_other
server: port = localhost:4304
```
 Create the folder where the 1 Wire devices will be mounted.
```bash
sudo mkdir /mnt/1wire

#and reboot it
sudo reboot
```
 Enable the owserver service
```bash
sudo systemctl enable owserver.service
```

Your 1 Wire devices can be found in the directory /mnt/1wire
```bash
$ls  /mnt/1wire
28.793434000000  alarm  settings      statistics  system
28.B9F533000000  bus.0  simultaneous  structure   uncached

#cat /mnt/1wire/28.793434000000/temperature
24.6875
```

if you see duplicate devices on the /mnt/1wire directory -- 
you need to edit the **/lib/systemd/system/owfs.service** file.

```bash
sudo nano /lib/systemd/system/owfs.service

#edit the line with ExecStart=/usr/bin/owfs -c /etc/owfs.conf --allow_other %t/owfs
# so it looks like
ExecStart=/usr/bin/owfs --allow_other %t/owfs

#save and reboot
```

## Install  piotserver

start by cloning the repository
 
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
 ./piotserver -d -p --assets garden -f garden.props.json
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
  
  
  
