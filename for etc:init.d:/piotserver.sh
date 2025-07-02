#!/bin/bash
# /etc/init.d/fan

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
        cd /home/vinnie/piotserver/
        nohup /home/vinnie/piotserver/piotserver -d -p --assets /home/vinnie/piotserver/garden -f garden.props.json &
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
