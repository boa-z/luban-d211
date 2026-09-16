#!/bin/sh
#
# Provision and bring up the onboard Wi-Fi (and optionally the 4G modem).
#
# Usage:
#   D211_SSID=my-ssid D211_PSK=my-pass d211-network.sh
#   d211-network.sh --ssid my-ssid --psk my-pass
#
# The generated wpa_supplicant configuration intentionally does not use
# ctrl_interface: this rootfs builds wpa_supplicant without the control
# interface support.
#

CONF=/etc/wpa_supplicant.conf
IFACE=wlan0

SSID="${D211_SSID:-}"
PSK="${D211_PSK:-}"

while [ $# -gt 0 ]; do
	case "$1" in
		--ssid)
			SSID="$2"
			shift 2
			;;
		--psk)
			PSK="$2"
			shift 2
			;;
		-h | --help)
			echo "Usage: D211_SSID=ssid D211_PSK=pass $0"
			exit 0
			;;
		*)
			echo "Unknown option: $1"
			exit 1
			;;
	esac
done

if [ -n "$SSID" ]; then
	umask 077
	{
		echo "network={"
		echo "	ssid=\"$SSID\""
		if [ -n "$PSK" ]; then
			echo "	psk=\"$PSK\""
		else
			echo "	key_mgmt=NONE"
		fi
		echo "}"
	} > "$CONF"
	echo "Wrote $CONF for SSID '$SSID'"
fi

echo "Restarting the Wi-Fi service"
/etc/init.d/S91wifi restart

for i in 1 2 3 4 5; do
	sleep 1
	[ -e "/sys/class/net/$IFACE" ] || continue
	addr=$(ifconfig "$IFACE" 2>/dev/null | grep -o "addr:[0-9.]*" | awk 'NR==1')
	[ -n "$addr" ] && {
		echo "$IFACE $addr"
		break
	}
done

cat /proc/net/dev | grep -E "^ *($IFACE|usb0):"
