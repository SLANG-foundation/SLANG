#!/bin/sh

bring_all_if_up()
{
	get_if_list
	for i in $if_list
	do
		ifconfig $i up
	done
}

get_if_stat()
{
	tmp=`ifconfig $1 |grep RUNNING`
	if [ "$?" = "0" ]
	then
		if_stat="connected"
	else
		if_stat="no-carrier"
	fi
}

get_if_list()
{
	if_list=`ip -o link | cut -d':' -f2|tr -d ' '`
}

menu_network()
{
	while true
	do
		bring_all_if_up
		get_if_list # sets if_list
		if_str=""
		for i in $if_list
		do
			get_if_stat $i # sets if_stat
			if_str="$if_str $i $if_stat"
		done
		dialog --title "SLA-NG Network Settings" --menu \
			"Choose an interface. You probably want an 'eth'." \
			20 60 13 $if_str \
			2>/tmp/ui.dialog
		if [ "$?" -eq 1 ]
		then
			break	
		fi
		if="`cat /tmp/ui.dialog`"
		menu_network_if
	done
}

menu_network_if()
{
	while true
	do
		dialog --title "SLA-NG Network Settings ($if)" --menu \
			"Choose between automaic (DHCP) or static address." \
			20 60 13 v "View Current Settings" \
			d "Set Automaic (DHCP) Address Mode" \
			s "Configure Static IP/IPv6 Addresses" \
			2>/tmp/ui.dialog
		if [ "$?" -eq 1 ]
		then
			break	
		fi
		if [ "`cat /tmp/ui.dialog`" = "v" ]	
		then
			dialog --msgbox "`ifconfig $if`" 20 75
		fi
		if [ "`cat /tmp/ui.dialog`" = "d" ]	
		then
			echo "Starting DHCP on $if." > /tmp/dhclient.log
			echo "Press EXIT when ready." >> /tmp/dhclient.log
			echo "----------------------" >> /tmp/dhclient.log
			dhclient $if 2>> /tmp/dhclient.log &
			dialog --tailbox /tmp/dhclient.log 20 75
		fi
		if [ "`cat /tmp/ui.dialog`" = "s" ]	
		then
			dialog --form "Type IP" 20 75 20 \
			"IP Address"   1  1 "172.16.0.100"     1 20 40 40 \
			"IP Netmask"   2  1 "255.255.255.0"    2 20 40 40 \
			"IP Gateway"   3  1 "172.16.0.1"       3 20 40 40 \
			"IPv6 Address" 5  1 "::1"              5 20 40 40 \
			"IPv6 Bitmask" 6  1 "128"              6 20 40 40 \
			"IPv6 Gateway" 7  1 "::1"              7 20 40 40 \
			"Nameserver 1" 9  1 "8.8.8.8"          9 20 40 40 \
			"Nameserver 2" 10 1 "130.244.127.169" 10 20 40 40 \
			"Nameserver 3" 11 1 ""                11 20 40 40 \
			2>/tmp/ui.dialog
		fi
	done
}

while true
do
	dialog  --title "SLA-NG Configuration Console" \
		--menu "Welcome! Navigate using arrow keys and TAB." \
		10 50 5 n "Network Settings" s "Start Shell (bash)"  \
		2>/tmp/ui.dialog
	if [ "$?" -eq 1 ]
	then
		exit
	fi
	if [ "`cat /tmp/ui.dialog`" = "n" ]
	then
		menu_network
	fi
	if [ "`cat /tmp/ui.dialog`" = "s" ]
	then
		clear # clear screen
		reset # reset terminal; dialog may fuck it up :)
		echo "Press CTRL+d to return to the console."
		bash
	fi
done

