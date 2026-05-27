piotserver - Raspberry Pi Internet of Things server

Farm automation made easy as Pi.

Articles

Off-Grid Farm Automation with Raspberry Pi
Farm automation made easy as Pi
Details

And these are the details:

RS-485 network to control my farm irrigation.
Controlling the Irrigation valves.
Reading the Temperature with 1-wire Sensors
Using the I2C protocol to communicate to sensors.
Reading Environmental Data with I2C
Rain Sensors
More I/O
Getting the data and controlling the devices from the REST API
Writing your own device plug-ins
Using software devices to build on the hardware.
How to make things happen automatically
How to make things happen on time
There are always conditions to evaluate
Rolling my own I²C and 1-Wire interface card
PWM, a better way to control sprinkler valves
Not Another Sprinkler Valve Article; TLDR
I2C Hardware Devices

TMP10X - Low-Power Digital Temperature Sensor
SHT25 - ±1.8% Digital humidity and temperature sensor
SHT30 - Digital humidity and temperature sensor with filter membrane
BME280 - Temperature Humidity Pressure Sensor
ADS1115 - 16 bit Analog-to-Digital Converter
MCP3427 - 2-Channel 16-Bit Analog to Digital Converter
MCP23008 - 8-Bit I/O Expander
PCA9536 - 4-Bit I/O Expander
PCA9671 - 16-Bit I/O Expander
QwiicButton - SparkFun Qwiic Button
QWIIC_RELAY - SparkFun Qwiic Relay
TCA9534 - SparkFun Qwiic GPIO
VALVENODE - RS-485 interface board to Irrigation Valves
Waveshare Raspberry Pi 8-ch Relay Expansion Board
Pseudo Devices

Sprinkler Controller
Actuator
Tank Depth Sensor
Hardware

nodeLynk I²C
SparkFun Qwiic
adafruit I²C
Iowa Scaled Engineering
Waveshare
Build system and hardware

piotserver runs on Raspberry Pi OS Bookworm.

Tested on:

Raspberry Pi Zero 2 W Rev 1.0
Raspberry Pi 5 Model B Rev 1.0
The current build system is plain GNU Make. CMake, Xcode project files, and Docker build files are no longer part of the supported build path.

This project talks directly to local hardware such as I2C and GPIO. For production use, run it directly on the host under systemd. Docker is not the preferred deployment model for this project because it adds device mapping, signal handling, and shutdown behavior complications around hardware access.

Installing and Building on Raspberry Pi

These steps assume a Raspberry Pi running Raspberry Pi OS or another Debian-based Linux install. piotserver is now built with plain Makefiles. CMake, Xcode project files, and Docker are no longer part of the supported build path.

Always run the upgrade

sudo apt update
sudo apt-get upgrade
sudo reboot
Install build tools and runtime support

piotserver is built with clang and uses SQLite, dynamic plugin loading, I2C, and GPIO access.

sudo apt install -y \
 clang-19 \
 make \
 git \
 sqlite3 \
 libsqlite3-dev \
 i2c-tools \
 gpiod \
 libgpiod-dev
Enable I2C

run

sudo raspi-config
Enable: **Interface Options / I2C **
and then reboot

Check GPIO access

Modern Raspberry Pi Linux exposes GPIO through /dev/gpiochip\*.

ls -l /dev/gpiochip\*
gpiodetect
gpioinfo
If running as a normal user, add that user to the usual Raspberry Pi hardware groups:

sudo usermod -aG i2c,gpio $USER
Log out and back in after changing groups.

Update the Raspberry Pi boot configuration

This project runs as a headless hardware controller. On current Raspberry Pi OS releases, the boot configuration file is usually:

sudo nano /boot/firmware/config.txt
This is the relevant configuration from my controller:

## For more options and information see

# http://rptl.io/configtxt

# Some settings may impact device functionality. See link above for details.

# Enable I2C for local hardware devices.

dtparam=i2c_arm=on

# Disable unused onboard features for a headless controller.

dtparam=audio=off
dtoverlay=disable-bt
camera_auto_detect=0
display_auto_detect=0
hdmi_ignore_hotplug=1
hdmi_blanking=2

# POWER_DOWN - output

# Assert GPIO25 when the Pi has halted so external power-control hardware

# can safely remove power.

dtoverlay=gpio-poweroff,gpiopin=25

# BAT_LOW - input

# Use GPIO23 as an active-low shutdown input with pull-up.

dtoverlay=gpio-shutdown,gpio_pin=23,active_low=1,gpio_pull=up

# Automatically load initramfs files, if found.

auto_initramfs=1

# Run in 64-bit mode.

arm_64bit=1

# Run as fast as firmware / board allows.

arm_boost=1

# Enable UART if serial console or local serial hardware is needed.

enable_uart=1
Be sure to reboot after making these changes.

Check power supply / battery

POWER_DOWN - gpiopin=25 is connected to the relay that shuts off the board. You can test this by doing a shutdown and seeing if the board shuts off.
sudo shutdown now
BAT_LOW - gpio_pin=23 is connected to the DRC battery low signal and will initiate a shutdown on the Raspberry Pi.

GPIO pin 24 is connected to AC OK on the DRC power supply. You can check this with:

# check the level with

gpioget --bias=pull-up GPIO24

# monitor it with

gpiomon --bias=pull-up GPIO24

# with AC applied this should return 0 (falling)

# when AC is unplugged, it will return 1 (rising)

Check daughter board FAULT signal

FAULT - gpiopin=22 is connected to the daughter board FAULT signal. You can check it by grounding pin 3 of the daughter board connector or shorting a DRV103 output while it is running. The red LED will illuminate and GPIO 22 will go low.

# check the level with

gpioget --bias=pull-up GPIO22

# monitor it with

gpiomon --bias=pull-up GPIO22
The Hardware UART

Note: I added enable_uart as a backup for when WiFi is dead and I cannot SSH.

If you plan to use this, you might need to set up a login password.

# change or setup your password

passwd

# you can check it by looking at the password file

cat /etc/passwd

# also note the console speed should be 115200

cat /boot/firmware/cmdline.txt
You should see something like:

console=serial0,115200 console=tty1 .....
You can check login with screen.

ls /dev/tty.\*

# example

screen /dev/tty.usbserial-1410 115200
Example console:

Debian GNU/Linux 13 xxxxxx ttyAMA0

My IP address is .....

xxxx login:
Exit screen:

Press Ctrl-A then K, then confirm with y.
Alternatively, Ctrl-A then \ quits all sessions.
WIFI power saving

I am disabling WiFi power saving. Maybe Raspberry Pi fixed this by now.

sudo nano /etc/NetworkManager/conf.d/wifi-powersave-off.conf
Add this:

[connection]
wifi.powersave = 2
After reboot, check it:

iw dev wlan0 get power_save
Expected:

Power save: off
Setting up the 1-Wire DS2482 OWFS driver

Check if your hardware is working. You should at least see the DS2482 at address 0x18.

sudo i2cdetect -y 1
Example:

     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f

00: -- -- -- -- -- -- -- --
10: -- -- -- -- -- -- -- -- 18 -- -- -- -- -- -- --
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
60: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- --
70: -- -- -- -- -- -- -- --
Install OWFS:

sudo apt-get update
sudo apt-get install owfs ow-shell
Edit owfs.conf to enable the I2C 1-Wire interface.

sudo nano /etc/owfs.conf
Comment out everything else and make sure it reads like this. You do not need the HTTP or FTP ports, although HTTP can be useful for debugging your 1-Wire devices.

######################## SOURCES ########################

#

# With this setup, any client (but owserver) uses owserver on the

# local machine...

! server: server = localhost:4304
server: device = /dev/i2c-1
######################### OWFS ##########################

#

mountpoint = /mnt/1wire
allow_other

#

server: port = localhost:4304
Create the folder where the 1-Wire devices will be mounted.

sudo mkdir /mnt/1wire
sudo reboot
Enable the owserver service.

sudo systemctl enable owserver.service
Your 1-Wire devices can be found in /mnt/1wire.

ls /mnt/1wire
cat /mnt/1wire/28.793434000000/temperature
Example:

28.793434000000 alarm settings statistics system
28.B9F533000000 bus.0 simultaneous structure uncached
24.6875
If you see duplicate devices in /mnt/1wire, edit the owfs.service file.

sudo nano /lib/systemd/system/owfs.service
Edit the line with:

ExecStart=/usr/bin/owfs -c /etc/owfs.conf --allow_other %t/owfs
so it looks like:

ExecStart=/usr/bin/owfs --allow_other %t/owfs
Then reboot.

Install piotserver

Start by cloning the repository.

cd
git clone https://github.com/vinthewrench/piotserver.git
cd piotserver
Build the application and all driver plugins:

make -j4
Build only the driver plugins:

make -j4 plugins
Clean the application, plugin object directories, and plugin products:

make clean
For a deeper cleanup that also removes generated auxiliary files and local runtime database files:

make distclean
The top-level makefile builds the piotserver executable and calls the individual driver Makefiles under drivers/<PLUGIN_NAME>.

Current driver plugins:

ADS1115
BME280
MCP23008
MCP3427
PCA9536
PCA9671
PWRGATE
QWIICBUTTON
QWIIC_RELAY
SAMPLE
SHT25
SHT30
TCA9534
TMP10X
VALVEMASTER
The QwiicButton driver directory builds the plugin product QWIICBUTTON.so on Linux.

Running piotserver

For a basic smoke test, use sample.props.json.

./piotserver -d -v -p -f sample.props.json
The sample property file demonstrates the three main property-file sections:

devices - plugin or hardware-backed devices.
values - server-managed values.
sequence - automation logic, including startup actions.
The sample includes:

SAMPLE device:
RUN_TIME

GPIO input:
POWER_FAIL on BCM 24

Server-managed value:
START_COUNT

Startup sequence:
START_COUNT := START_COUNT + 1
For real ValveNode irrigation hardware, use the ValveNode property file.

./piotserver -d -v -p -f valvenode.props.json
ValveNode and VALVEMASTER

VALVEMASTER is the pIoTServer plugin for my ValveNode irrigation system. It talks to a Valve Master board over I2C. The Valve Master handles RS-485 field-bus power, node discovery, valve commands, and communication with the remote valve nodes.

You do not have to use my ValveNode hardware to use pIoTServer. pIoTServer can work with other devices and plugins. ValveNode is one supported hardware path for irrigation control.

If you decide to use my ValveNode hardware, the related firmware and board files live in the separate ValveNode repository:

https://github.com/vinthewrench/valvenode
The valvenode.props.json file is the pIoTServer configuration that maps server keys such as SPRK_5 to ValveNode RS-485 node/channel pairs through the VALVEMASTER plugin.

On stop or shutdown, the VALVEMASTER driver synchronously queues a close-all operation, waits for completion, waits briefly for close-all settle, then turns off field power. That behavior is important for latching irrigation valves and is one reason the preferred deployment is a normal host process under systemd, not Docker.

Running as a systemd service

Once the property file is set up and the devices are working, install piotserver as a systemd service.

Create /etc/systemd/system/piotserver.service:

[Unit]
Description=pIoTServer
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
WorkingDirectory=/home/vinnie/piotserver
ExecStart=/home/vinnie/piotserver/piotserver -d -v -p -f sample.props.json
Restart=on-failure
RestartSec=5

# Let SIGINT reach the app so drivers such as VALVEMASTER can shut down safely.

KillSignal=SIGINT
TimeoutStopSec=45

[Install]
WantedBy=multi-user.target
For real irrigation hardware, change the ExecStart property file to valvenode.props.json or the property file you actually use.

Install and start the service:

sudo systemctl daemon-reload
sudo systemctl enable piotserver.service
sudo systemctl start piotserver.service
Check status and logs:

sudo systemctl status piotserver.service
journalctl -u piotserver.service -f
Stop or restart:

sudo systemctl stop piotserver.service
sudo systemctl restart piotserver.service
Build paths no longer used

Hasta la vista, CMake. Don’t let the Xcode workspace hit you on the way out.

The current build path is GNU Make only.

Removed legacy paths:

CMake project files
Xcode project/workspace files
Docker build and compose files
old /etc/init.d service script pattern
For this project, Docker is not the preferred deployment model. piotserver needs direct access to I2C, GPIO, driver plugins, local state, and safe shutdown behavior. A host binary managed by systemd is the cleaner path.
