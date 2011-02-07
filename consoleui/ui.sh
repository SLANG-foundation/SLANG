#!/bin/bash

# DISK FUNCTIONS
disk_rw()
{
	fsck -p / &>> /tmp/log.dialog
	mount -o remount,rw / &>> /tmp/log.dialog
}

disk_ro()
{
	mount -o remount,ro / &>> /tmp/log.dialog
}

# NETWORK FUNCTIONS
netconf_remove()
{
	cp /etc/network/interfaces /tmp/net
	# Convert file to one-liners only
	sed -n '1h;1!H;${;g;s/\n\t/SLANGTAB/g;p;}' /tmp/net > /tmp/net2
	# Remove the 'auto' line
	grep -v "auto $1$" /tmp/net2 > /tmp/net
	# Remove the interface config line
	grep -v "iface $1 " /tmp/net > /tmp/net2
	# Write back config, multi-line
	sed 's/SLANGTAB/\n\t/g' /tmp/net2 > /etc/network/interfaces
}

netconf_static()
{
	disk_rw
	netconf_remove $if
	echo "auto $if" >> /etc/network/interfaces
	ip=`sed -n "1p" /tmp/ui.dialog`
	if [ "$ip" != "" ]
	then
		echo "iface $if inet static" >> /etc/network/interfaces
		echo -e "\taddress $ip" >> /etc/network/interfaces
		mask=`sed -n "2p" /tmp/ui.dialog`
		echo -e "\tnetmask $mask" >> /etc/network/interfaces
		gw=`sed -n "3p" /tmp/ui.dialog`
		echo -e "\tgateway $gw" >> /etc/network/interfaces
	fi
	ip=`sed -n "4p" /tmp/ui.dialog`
	if [ "$ip" != "" ]
	then
		echo "iface $if inet6 static" >> /etc/network/interfaces
		echo -e "\taddress $ip" >> /etc/network/interfaces
		mask=`sed -n "5p" /tmp/ui.dialog`
		echo -e "\tnetmask $mask" >> /etc/network/interfaces
		gw=`sed -n "6p" /tmp/ui.dialog`
		echo -e "\tgateway $gw" >> /etc/network/interfaces
	fi
	disk_ro
	show="no"
	ifdown $if &>> /tmp/log.dialog
	if [ "$?" != "0" ]
	then
		show="yes"
		exit
	fi
	ifup $if &>> /tmp/log.dialog
	if [ "$?" != "0" ]
	then
		show="yes"
		exit
	fi
	if [ "$show" = "yes" ]
	then
		echo "----------------------" >> /tmp/log.dialog
		date >> /tmp/log.dialog
		echo "Setup of $1 failed." >> /tmp/log.dialog
		echo "----------------------" >> /tmp/log.dialog
		dialog --tailbox /tmp/log.dialog 20 75
	fi
}

bring_all_if_up()
{
	get_if_list
	for i in $if_list
	do
		ifconfig $i up 2>&1 > /dev/null
	done
}

get_if_stat()
{
	tmp=`mii-tool $1 |grep "link ok"`
	if [ "$?" = "0" ]
	then
		if_stat=`mii-tool eth0|cut -d ' ' -f3`
	else
		if_stat="no-link"
	fi
}

get_if_list()
{
	if_list=`ip -o link | cut -d':' -f2|tr -d ' '`
}

# FORK FUNCTIONS, THAT IS RUN IN THE BACKGROUND FOR THE SAKE OF LOG
fork_dhcp()
{
	echo "----------------------" >> /tmp/log.dialog
	date >> /tmp/log.dialog
	echo "Starting DHCP on $1." >> /tmp/log.dialog
	echo "One one port can use DHCP." >> /tmp/log.dialog
	echo "Press EXIT when ready." >> /tmp/log.dialog
	echo "----------------------" >> /tmp/log.dialog
	pkill -9 dhclient
	disk_rw
	# Write configration 
	netconf_remove $1
	echo "auto $1" >> /etc/network/interfaces
	echo "iface $1 inet dhcp" >> /etc/network/interfaces
	# Sleep, in order for previous messages to be shown
	sleep 3
	# Run DHCP client, allow it to write /etc/resolv.conf
	ifdown $1 &>> /tmp/log.dialog
	ifup $1 &>> /tmp/log.dialog
	# Kill the DHCP client in order to mount read-only
	pkill -9 dhclient
	disk_ro
	echo "Complete." >> /tmp/log.dialog
	# Now that we are read-only, we can restart dhclient
	ifdown $1 &>> /dev/null 
	ifup $1 &>> /dev/null
}
if [ "$1" = "dhcp" ]
then
	fork_dhcp $2 
	exit
fi

# MENU  
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
			dialog --msgbox "`ip addr show $if`" 20 75
		fi
		if [ "`cat /tmp/ui.dialog`" = "d" ]	
		then
			# Fork DHCP settings
			$0 dhcp $if & 
			dialog --tailbox /tmp/log.dialog 20 75
		fi
		if [ "`cat /tmp/ui.dialog`" = "s" ]	
		then
			dialog --form "Type IP" 20 75 20 \
			"IP Address"   1  2 "172.16.0.100"     1 20 40 40 \
			"IP Netmask"   2  2 "255.255.255.0"    2 20 40 40 \
			"IP Gateway"   3  2 "172.16.0.1"       3 20 40 40 \
			"IPv6 Address" 5  2 "::1"              5 20 40 40 \
			"IPv6 Bitmask" 6  2 "128"              6 20 40 40 \
			"IPv6 Gateway" 7  2 "::1"              7 20 40 40 \
			2> /tmp/ui.dialog
			if [ "$?" = "1" ]
			then
				continue
			fi
			netconf_static
		fi
	done
}

while true
do
	dialog  --title "SLA-NG Configuration Console" \
		--menu "Welcome! Navigate using arrow keys and TAB." \
		15 50 10 \
		n "Network Interface Settings" \
		d "DNS Settings" \
		p "Ping Host" \
		s "Start Shell (bash)"  \
		2> /tmp/ui.dialog
	if [ "$?" -eq 1 ]
	then
		exit
	fi
	if [ "`cat /tmp/ui.dialog`" = "n" ]
	then
		menu_network
	fi
	if [ "`cat /tmp/ui.dialog`" = "d" ]	
	then
		s=`grep search /etc/resolv.conf | sed -n "1p"`
		s=`echo $s | sed "s/search //"`
		d1=`grep nameserver /etc/resolv.conf | sed -n "1p"`
		d1=`echo $d1 | sed "s/nameserver //"`
		d2=`grep nameserver /etc/resolv.conf | sed -n "2p"`
		d2=`echo $d2 | sed "s/nameserver //"`
		d3=`grep nameserver /etc/resolv.conf | sed -n "3p"`
		d3=`echo $d3 | sed "s/nameserver //"`
		dialog --form "Type the name servers to be used." 11 71 4 \
		"Search domain" 1 2 "$s"  1 20 40 40 \
		"Nameserver #1" 2 2 "$d1" 2 20 40 40 \
		"Nameserver #2" 3 2 "$d2" 3 20 40 40 \
		"Nameserver #3" 4 2 "$d3" 4 20 40 40 \
		2> /tmp/ui.dialog
		if [ "$?" = "1" ]
		then
			continue
		fi
		disk_rw
		echo -n "" > /etc/resolv.conf
		s="yes"
		for dns in `cat /tmp/ui.dialog`
		do
			if [ "$s" = "yes" ]
			then
				echo "search $dns" >>  /etc/resolv.conf
				s="no"
			else
				echo "nameserver $dns" >> /etc/resolv.conf
			fi
		done
		disk_ro
	fi
	if [ "`cat /tmp/ui.dialog`" = "p" ]
	then
		dialog --inputbox "Type an address to ping" \
		7 40 www.tele2.se 2> /tmp/ui.dialog
		ping -c 10 `cat /tmp/ui.dialog` &> /tmp/ui.dialog &
		dialog --tailbox /tmp/ui.dialog 20 75
		pkill -9 ping
	fi
	if [ "`cat /tmp/ui.dialog`" = "s" ]
	then
		clear # clear screen
		echo "Starting bash..."
		reset # reset terminal; dialog may fuck it up :)
		echo "Press CTRL+d to return to the console."
		bash
	fi
done

