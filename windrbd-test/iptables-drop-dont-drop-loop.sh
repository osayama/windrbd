#!/bin/bash
# run as root on a Linux host port is hardcoded to 7600
i=0
# PORT=7600
# RES=w0
PORT=7695
RES=windows10-boot

while true
do 
	i=$[ $i+1 ]
	echo "network failure $i"
	echo "about to drop packets"
	./iptables-drop-port.sh $PORT
	sleep 30	# ping timeout
	drbdadm status
	echo "about to not drop packets"
	./iptables-dont-drop-port.sh $PORT
	sudo drbdadm wait-sync $RES
	drbdadm status
	sleep 20	# so we can do something ...
	drbdadm status
done
