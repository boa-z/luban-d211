#!/bin/sh

IFNAME=$1
CMD=$2
echo "enter p2p-action.sh "
echo $IFNAME
echo $CMD

if [ "$CMD" = "P2P-GROUP-STARTED" ]; then
        GIFNAME=$3
        echo "p2p-action.sh:$CMD"

        #TODO
        if [ "$4" = "GO" ]; then
                # ifconfig $GIFNAME 192.168.49.1
                # sleep 1
                # killall -9 udhcpd
                # udhcpd /etc/udhcpd_p2p.conf

                GIFNAME="wlan1"
				echo "1";
                # 设置 IP 并启用接口
               # ip addr add 192.168.49.1/24 dev $GIFNAME
        #       ip link set dev $GIFNAME up 
		        ifconfig $GIFNAME 192.168.49.1
                rm -f /tmp/udhcpd.log
                killall udhcpd 2>/dev/null
                sleep 1
                killall -9 udhcpd 2>/dev/null
                # 等待接口就绪
               # while [ ! -d /sys/class/net/$GIFNAME ]; do sleep 0.1; done
			    touch /tmp/udhcpd.leases
			    touch /tmp/udhcpd.log
                udhcpd -f -S  /etc/udhcpd_p2p.conf > /tmp/udhcpd.log 2>&1 &
				echo "udpcpd started"
        fi
        if [ "$4" = "client" ]; then
                killall -9 udhcpc
                udhcpc -i $GIFNAME
        fi
fi

if [ "$CMD" = "P2P-GROUP-REMOVED" ]; then
        GIFNAME=$3
        echo "p2p-action.sh:$CMD"
        if [ "$4" = "GO" ]; then
                killall -9 udhcpd
        fi
        if [ "$4" = "client" ]; then
                killall -9 udhcpc
        fi
        ifconfig $GIFNAME 0
        #ip -6 addr flush dev $GIFNAME
fi

if [ "$CMD" = "AP-STA-DISCONNECTED" ]; then
        ifconfig $IFNAME 0
        #ip -6 addr flush dev $GIFNAME
fi
