#!/bin/sh

DAEMON="aesdsocket"

start() {
	printf 'Starting %s: ' "$DAEMON"
	start-stop-daemon -S -x "/usr/bin/$DAEMON" -- -d
	status=$?
	if [ "$status" -eq 0 ]; then
		echo "OK"
	else
		echo "FAIL"
	fi
	return "$status"
}

stop() {
	printf 'Stopping %s: ' "$DAEMON"
	start-stop-daemon -K -n $DAEMON
	status=$?
	if [ "$status" -eq 0 ]; then
		echo "OK"
	else
		echo "FAIL"
	fi
	return "$status"
}

restart() {
	stop
	sleep 1
	start
}

status() {
	printf 'Status %s: ' "$DAEMON"
	start-stop-daemon -T -n $DAEMON
	status=$?
	if [ "$status" -eq 0 ]; then
		echo "OK"
	else
		echo "FAILED"
	fi
	return "$status"

}

case "$1" in
	start|stop|restart|status)
		"$1"
		;;
	reload)
		# Restart, since there is no true "reload" feature.
		restart
		;;
	*)
		echo "Usage: $0 {start|stop|restart|reload|status}"
		exit 1
esac
