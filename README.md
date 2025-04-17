# piotserver - Raspberry Pi Internet of Things server 

write some stuff here

# Features

# Articles

pointer to substack

## Status

##### Always run the upgrade

```bash
$sudo apt update
$sudo apt-get upgrade
```

##### Install the LLVM toolchain

I chose to use [19.1.4](https://github.com/llvm/llvm-project/releases/) as my last reliable build system. 

sudo apt-get install clang-19
sudo update-alternatives --install /usr/bin/clang clang /usr/bin/clang-19 100
sudo update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-19 100

sudo update-alternatives --config clang
sudo update-alternatives --config clang++

clang --version
clang++ --version


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
# Some settings may impact device functionality. See link above for details

dtparam=i2c_arm=on

dtoverlay=w1-gpio,gpiopin=4

```

##### A few more libraries 
```bash
#GPIO development 

sudo apt install -y cmake
sudo apt install -y htop
sudo apt-get install -y i2c-tools  
sudo apt-get install -y  git-core
sudo apt-get install -y  sqlite3  
sudo apt-get install -y  libsqlite3-dev

```

##### The piotserver App

All this is why we came here.

 ```bash
cd
#clone the repository
git clone https://github.com/vinthewrench/piotserver.git
cd piotserver

#build it
mkdir build
cd build
cmake ..
make

# test it
bin/carradio
 ```

make it a system service for auto run on boot

copy this into  /etc/init.d/carradio

 ```bash
#!/bin/sh
### BEGIN INIT INFO

# Provides:carradio
# Required-Start:$local_fs $network $remote_fs $syslog  +shairport-sync
# Required-Stop:$local_fs $network $remote_fs $syslog
# Default-Start:2 3 4 5
# Default-Stop:0 1 6
# Short-Description: carradio
# Description: car radio auto start after boot
### END INIT INFO

case "$1" in
    start)
        echo "Starting carradio"
        cd /home/vinthewrench/carradio/build/bin
        sudo nohup /home/vinthewrench/carradio/build/bin/carradio &
         ;;
    stop)
        echo "Stopping carradio"
         sudo killall carradio
         ;;
    *)
        echo "Usage: service carradio start|stop"
        exit 1
        ;;
esac
exit 0
 ```

 ```bash
sudo chmod +x /etc/init.d/carradio
sudo update-rc.d carradio defaults

# to remove it
#sudo update-rc.d  -f carradio remove
 ```
`sudo service carradio start # start the service`
`sudo service carradio stop # stop the service`
`sudo service carradio restart # restart the service`
`sudo service carradio status # check service status`
 ```

