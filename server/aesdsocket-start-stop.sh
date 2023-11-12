#!/bin/sh

DAEMON="aesdsocket"

case "$1" in
	start)
		echo "Starting $DAEMON"
		start-stop-daemon -S -n $DAEMON -a /usr/bin/$DAEMON -- -d
		;;
	stop)
		echo "Stopping $DAEMON"
		start-stop-daemon -K -n $DAEMON
		;;
	restart)
		"$0" stop
		"$0" start
		;;
	*)
		echo "Usage: $0 {start|stop|restart}"
		exit 1
		;;
esac
