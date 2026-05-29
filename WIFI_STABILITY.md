###### Raspberry Pi Wi-Fi stability setup

For a Raspberry Pi field controller, Wi-Fi should be treated as part of the appliance setup, not as a casual desktop connection. This is especially important for systems expected to recover after an access point, mesh node, router, or DHCP service briefly drops and comes back.

This example assumes a node named:

```text
irrigation
```

with a reserved DHCP address:

```text
192.168.0.50
```

and a LAN gateway:

```text
192.168.0.1
```

Adjust those values for the actual site.

###### Network assumptions

Recommended setup:

```text
Hostname:       irrigation
Interface:      wlan0
Wi-Fi SSID:     your-site-ssid
Reserved IP:    192.168.0.50
Gateway:        192.168.0.1
```

Reserve the IP address in the router, DHCP server, or UniFi controller. Do not rely on a random DHCP address for a controller that other machines need to reach.

Verify the current network state:

```bash
hostname
ip addr show wlan0
ip route
iw dev wlan0 link
```

Expected shape:

```text
wlan0 has the reserved IP address
default route points to the LAN gateway
Wi-Fi signal is reasonable
```

A signal around `-40` to `-60 dBm` is good. Around `-70 dBm` is workable. Worse than `-75 dBm` is asking for stupid failures.

###### Disable Wi-Fi power saving

Disable Wi-Fi power saving immediately:

```bash
sudo iw dev wlan0 set power_save off
```

Verify:

```bash
iw dev wlan0 get power_save
```

Expected:

```text
Power save: off
```

Make the setting persistent with a systemd service:

```bash
sudo nano /etc/systemd/system/wifi-powersave-off.service
```

```ini
[Unit]
Description=Disable Wi-Fi power saving on wlan0
After=sys-subsystem-net-devices-wlan0.device
Wants=sys-subsystem-net-devices-wlan0.device

[Service]
Type=oneshot
ExecStart=/usr/sbin/iw dev wlan0 set power_save off
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```

Enable it:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now wifi-powersave-off.service
```

Verify:

```bash
systemctl status wifi-powersave-off.service --no-pager
iw dev wlan0 get power_save
```

###### Confirm NetworkManager

Current Raspberry Pi OS installations commonly use NetworkManager.

Check:

```bash
systemctl is-active NetworkManager
systemctl is-active dhcpcd
systemctl is-active systemd-networkd
nmcli device status
```

This watchdog example assumes NetworkManager is active and `wlan0` is managed by NetworkManager.

Also confirm the Wi-Fi connection is set to autoconnect:

```bash
nmcli -t -f NAME,UUID,TYPE,AUTOCONNECT,DEVICE connection show
```

The active Wi-Fi connection should show:

```text
802-11-wireless:yes:wlan0
```

###### Add a Wi-Fi watchdog

Install a watchdog script:

```bash
sudo nano /usr/local/sbin/wifi-watchdog.sh
```

```bash
#!/bin/bash

set -u

IFACE="wlan0"
PING_TARGET="192.168.0.1"
STATE_DIR="/run/wifi-watchdog"
FAIL_FILE="${STATE_DIR}/fail_count"

mkdir -p "$STATE_DIR"

fail_count=0
if [ -f "$FAIL_FILE" ]; then
    fail_count="$(cat "$FAIL_FILE" 2>/dev/null || echo 0)"
fi

if ping -I "$IFACE" -c 2 -W 2 "$PING_TARGET" >/dev/null 2>&1; then
    echo 0 > "$FAIL_FILE"
    exit 0
fi

fail_count=$((fail_count + 1))
echo "$fail_count" > "$FAIL_FILE"

logger -t wifi-watchdog "Wi-Fi/LAN check failed on ${IFACE}, count=${fail_count}"

if [ "$fail_count" -eq 1 ]; then
    logger -t wifi-watchdog "Reapplying NetworkManager connection on ${IFACE}"
    nmcli device reapply "$IFACE" >/dev/null 2>&1 || true
    exit 0
fi

if [ "$fail_count" -eq 2 ]; then
    logger -t wifi-watchdog "Disconnecting/reconnecting ${IFACE}"
    nmcli device disconnect "$IFACE" >/dev/null 2>&1 || true
    sleep 5
    nmcli device connect "$IFACE" >/dev/null 2>&1 || true
    exit 0
fi

if [ "$fail_count" -eq 3 ]; then
    logger -t wifi-watchdog "Restarting NetworkManager"
    systemctl restart NetworkManager
    exit 0
fi

if [ "$fail_count" -ge 6 ]; then
    logger -t wifi-watchdog "Still offline after repeated recovery attempts, rebooting"
    echo 0 > "$FAIL_FILE"
    /sbin/reboot
fi

exit 0
```

Set the gateway address for the actual LAN:

```bash
PING_TARGET="192.168.0.1"
```

Use the local gateway, not a public internet address. The purpose is to recover local Wi-Fi/LAN connectivity, not test whether the WAN connection is alive.

Make the script executable:

```bash
sudo chmod +x /usr/local/sbin/wifi-watchdog.sh
```

Run it once manually:

```bash
sudo /usr/local/sbin/wifi-watchdog.sh
echo $?
journalctl -t wifi-watchdog --no-pager -n 20
```

Expected result while the network is healthy:

```text
0
```

There may be no `wifi-watchdog` log entries. That is normal. The script only logs recovery actions after failures.

###### Run the watchdog from systemd

Create the service:

```bash
sudo nano /etc/systemd/system/wifi-watchdog.service
```

```ini
[Unit]
Description=Wi-Fi recovery watchdog

[Service]
Type=oneshot
ExecStart=/usr/local/sbin/wifi-watchdog.sh
```

Create the timer:

```bash
sudo nano /etc/systemd/system/wifi-watchdog.timer
```

```ini
[Unit]
Description=Run Wi-Fi recovery watchdog every minute

[Timer]
OnBootSec=2min
OnUnitActiveSec=1min
AccuracySec=10s
Unit=wifi-watchdog.service

[Install]
WantedBy=timers.target
```

Enable it:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now wifi-watchdog.timer
```

Verify:

```bash
systemctl status wifi-watchdog.timer --no-pager
systemctl status wifi-watchdog.service --no-pager
systemctl list-timers | grep wifi
```

###### Post-reboot verification

After reboot, verify:

```bash
hostname
ip addr show wlan0
ip route
iw dev wlan0 get power_save
iw dev wlan0 link
systemctl status wifi-powersave-off.service --no-pager
systemctl status wifi-watchdog.timer --no-pager
```

Expected:

```text
hostname is correct
wlan0 has the reserved DHCP address
default route points to the LAN gateway
Wi-Fi power save is off
wifi-powersave-off.service is enabled and successful
wifi-watchdog.timer is active
```

###### Application service note

For local hardware controllers, the application should usually not require `network-online.target`.

For example, a local irrigation controller talking to hardware over I2C should be able to start even if Wi-Fi is temporarily unavailable.

Prefer:

```ini
[Unit]
Description=Application Service
After=multi-user.target
```

Avoid making the service depend unnecessarily on:

```ini
After=network-online.target
Wants=network-online.target
```

The network is how users reach the controller. It should not be required for the local controller process to start.
