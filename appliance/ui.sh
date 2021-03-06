#!/bin/sh

# SUDO ITSELF
if [ "$1" = "" -a `id -u` -ne 0 ]
then
	sudo $0 noloop
	exit
fi

ui_printerr()
{
	if [ "$?" -ne 0 ]
	then
		dialog --msgbox "`cat /tmp/log.dialog`" 20 70
	fi
}

# DISK FUNCTIONS
disk_rw()
{
	echo "Mounting R/W..." | dialog --progressbox 3 30
	fsck -p / > /tmp/log.dialog 2>&1
	ui_printerr
	mount -o remount,rw / > /tmp/log.dialog 2>&1
	ui_printerr
}

disk_ro()
{
	echo "Mounting R/O..." | dialog --progressbox 3 30
	mount -o remount,ro / > /tmp/log.dialog 2>&1
	ui_printerr
}

# NETWORK FUNCTIONS
netconf_restart()
{
	ifdown $1 > /tmp/log.dialog 2>&1
	ui_printerr
	ifup $1 > /tmp/log.dialog 2>&1
	ui_printerr
}

netconf_restart_quiet()
{
	ifdown $1 > /dev/null 2>&1
	ifup $1 > /dev/null 2>&1
}

netconf_remove()
{
	echo "Removing $1..." | dialog --progressbox 3 30
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
	echo "Adding $if..." | dialog --progressbox 3 30
	echo "auto $if" >> /etc/network/interfaces
	ip=`sed -n "1p" /tmp/ui.dialog`
	if [ "$ip" != "" ]
	then
		echo "iface $if inet static" >> /etc/network/interfaces
		printf "\taddress $ip\n" >> /etc/network/interfaces
		mask=`sed -n "2p" /tmp/ui.dialog`
		printf "\tnetmask $mask\n" >> /etc/network/interfaces
	fi
	gw=`sed -n "3p" /tmp/ui.dialog`
	if [ "$gw" != "" ]
	then
		printf "\tgateway $gw\n" >> /etc/network/interfaces
	fi
	ip=`sed -n "4p" /tmp/ui.dialog`
	if [ "$ip" != "" ]
	then
		printf "iface $if inet6 static\n" >> /etc/network/interfaces
		printf "\taddress $ip\n" >> /etc/network/interfaces
		mask=`sed -n "5p" /tmp/ui.dialog`
		printf "\tnetmask $mask\n" >> /etc/network/interfaces
	fi
	gw=`sed -n "6p" /tmp/ui.dialog`
	if [ "$gw" != "" ]
	then
		printf "\tgateway $gw\n" >> /etc/network/interfaces
	fi
	disk_ro
	echo "Restarting $if..." | dialog --progressbox 3 30
	netconf_restart $if
}

netconf_dhcp()
{
	disk_rw
	netconf_remove $if
	echo "Adding $if..." | dialog --progressbox 3 30
	echo "auto $if" >> /etc/network/interfaces
	echo "iface $if inet dhcp" >> /etc/network/interfaces
	# Run DHCP client, allow it to write /etc/resolv.conf
	echo "Request DHCP, R/W..." | dialog --progressbox 3 30
	ifdown $if > /tmp/log.dialog 2>&1
	ui_printerr
	ifup $if > /tmp/log.dialog 2>&1
	dialog --msgbox "`cat /tmp/log.dialog`" 20 70
	# Kill the DHCP client in order to mount read-only
	pkill -9 dhclient
	disk_ro
	# Now that we are read-only, we can restart dhclient
	echo "Restart DHCP, R/O..." | dialog --progressbox 3 30
	netconf_restart_quiet $if
}

netconf_dns()
{
	echo "Saving..." | dialog --progressbox 3 30
	grep -v "^search " /etc/resolv.conf > /tmp/net
	grep -v "^nameserver " /tmp/net > /tmp/net2
	search=`sed -n "1p" /tmp/ui.dialog`
	if [ "$search" != "" ]
	then
		echo "search $search" >> /tmp/net2
	fi
	for dns in `tail -n +2 /tmp/ui.dialog`
	do
		echo "nameserver $dns" >> /tmp/net2
	done
	disk_rw
	cp /tmp/net2 /etc/resolv.conf
	disk_ro
}

netconf_bring_all_if_up()
{
	netconf_get_if_list
	for i in $if_list
	do
		ip link set $i up > /dev/null 2>&1
	done
}

netconf_get_if_stat()
{
	tmp=`mii-tool $1 2> /dev/null |grep "link ok"`
	if [ "$?" -eq 0 ]
	then
		if_stat=`mii-tool $1 2> /dev/null | cut -d ' ' -f3`
	else
		if_stat="no-link"
	fi
}

netconf_get_if_list()
{
	if_list=`ip -o link | grep -v "1: lo:" | cut -d':' -f2|tr -d ' '`
}

# MENU
menu_network()
{
	while true
	do
		netconf_bring_all_if_up # in order to see link status
		netconf_get_if_list # sets if_list
		if_str=""
		for i in $if_list
		do
			netconf_get_if_stat $i # sets if_stat
			if_str="$if_str $i $if_stat"
		done
		dialog --title "SLA-NG Network Settings" --menu \
			"Choose an interface. You probably want an 'eth'." \
			20 60 13 $if_str \
			2>/tmp/ui.dialog
		if [ "$?" -ne 0 ]
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
		if [ "$?" -ne 0 ]
		then
			break	
		fi
		if [ "`cat /tmp/ui.dialog`" = "v" ]	
		then
			dialog --msgbox "`ip addr show $if`" 20 75
		fi
		if [ "`cat /tmp/ui.dialog`" = "d" ]	
		then
			netconf_dhcp
		fi
		if [ "`cat /tmp/ui.dialog`" = "s" ]	
		then
			menu_network_if_static
		fi
	done
}

menu_network_if_static()
{
	# Extract IPv4 info 
	cp /etc/network/interfaces /tmp/net
	# Convert file to one-liners only
	sed -n '1h;1!H;${;g;s/\n\t/SLANGTAB/g;p;}' /tmp/net > /tmp/net2
	# Get right config line 
	grep "iface $if inet " /tmp/net2 > /tmp/net
	# Write back config, multi-line
	sed 's/SLANGTAB/\n\t/g' /tmp/net > /tmp/net2
	ip=`grep "address " /tmp/net2 | cut -d' ' -f2`
	mask=`grep "netmask " /tmp/net2 | cut -d' ' -f2`
	gw=`grep "gateway " /tmp/net2 | cut -d' ' -f2`
	# Extract IPv6 info 
	cp /etc/network/interfaces /tmp/net
	# Convert file to one-liners only
	sed -n '1h;1!H;${;g;s/\n\t/SLANGTAB/g;p;}' /tmp/net > /tmp/net2
	# Get right config line 
	grep "iface $if inet6 " /tmp/net2 > /tmp/net
	# Write back config, multi-line
	sed 's/SLANGTAB/\n\t/g' /tmp/net > /tmp/net2
	ip6=`grep "address " /tmp/net2 | cut -d' ' -f2`
	mask6=`grep "netmask " /tmp/net2 | cut -d' ' -f2`
	gw6=`grep "gateway " /tmp/net2 | cut -d' ' -f2`
	dialog --form "Type the addresses. Leave fields empty to omit them." \
		14 70 7 \
		"IP Address"     1  2 "$ip"     1 20 43 100 \
		"IP Netmask"     2  2 "$mask"   2 20 43 100 \
		"IP Gateway"     3  2 "$gw"     3 20 43 100 \
		"IPv6 Address"   5  2 "$ip6"    5 20 43 100 \
		"IPv6 Prefixlen" 6  2 "$mask6"  6 20 43 100 \
		"IPv6 Gateway"   7  2 "$gw6"    7 20 43 100 \
		2> /tmp/ui.dialog
	if [ "$?" -eq 0 ]
	then
		netconf_static
	fi
}

menu_dns()
{
	s=`grep search /etc/resolv.conf | sed -n "1p"`
	s=`echo $s | sed "s/search //"`
	d1=`grep nameserver /etc/resolv.conf | sed -n "1p"`
	d1=`echo $d1 | sed "s/nameserver //"`
	d2=`grep nameserver /etc/resolv.conf | sed -n "2p"`
	d2=`echo $d2 | sed "s/nameserver //"`
	d3=`grep nameserver /etc/resolv.conf | sed -n "3p"`
	d3=`echo $d3 | sed "s/nameserver //"`
	dialog --form "Type the name servers to be used." 11 71 4 \
	"Search domain" 1 2 "$s"  1 20 40 140 \
	"Nameserver #1" 2 2 "$d1" 2 20 40 140 \
	"Nameserver #2" 3 2 "$d2" 3 20 40 140 \
	"Nameserver #3" 4 2 "$d3" 4 20 40 140 \
	2> /tmp/ui.dialog
	if [ "$?" -eq 0 ]
	then
		netconf_dns
	fi
}

menu_timezone()
{
	disk_rw
	dpkg-reconfigure tzdata
	disk_ro
}

menu_hostname()
{
	dialog --inputbox "Type a host name (not FQDN)." \
	9 40 "`cat /etc/hostname`" 2> /tmp/ui.dialog
	if [ "$?" -eq 0 ]
	then
		disk_rw
		cp /tmp/ui.dialog /etc/hostname
		hostname "`cat /etc/hostname`"
		disk_ro
	fi
}

menu_cfg()
{
	c1=`sed -n "1p" /etc/sla-ng/manager.conf`
	c2=`sed -n "2p" /etc/sla-ng/manager.conf`
	c3=`sed -n "3p" /etc/sla-ng/manager.conf`
	c4=`sed -n "4p" /etc/sla-ng/manager.conf`
	c5=`sed -n "5p" /etc/sla-ng/manager.conf`
	dialog --form "The actual 'probed' configuration (containing the\
	measurement sessions; 'pings') is downloaded from an SLA-NG\
	management server, via an XML-RPM API. The settings below\
	specify how to communicate with the SLA-NG manager. If you\
	like to edit the 'probed' configuration manually, you find\
	it in /tmp/probed.conf by default." 17 71 5 \
	"Manager XML-RPC URL"    1 2 "$c1" 1 30 35 210 \
	"Manager shared secret"  2 2 "$c2" 2 30 35 210 \
	"Probe TCP/UDP port"     3 2 "$c3" 3 30 35 210 \
	"Probe timestamp type"   4 2 "$c4" 4 30 35 210 \
	"Timestamping interface" 5 2 "$c5" 5 30 35 210 \
	2> /tmp/ui.dialog
	if [ "$?" -eq 0 ]
	then
		disk_rw
		cp /tmp/ui.dialog /etc/sla-ng/manager.conf
		disk_ro
	fi
}


menu_log()
{
	dialog --tailbox /var/log/messages 20 75
}

menu_view()
{
	dialog --inputbox "Type a measurement session ID, or -1  to show\
	all results, irrespective of session ID." \
	9 40 "-1" 2> /tmp/ui.dialog
	if [ "$?" -eq 0 ]
	then
		sla-ng-view -i `cat /tmp/ui.dialog` > /tmp/ui.dialog 2>&1 &
		dialog --tailbox /tmp/ui.dialog 20 75
		pkill -9 -f sla-ng-view
	fi
}
menu_aggr()
{
	dialog --inputbox "Type a measurement session ID." \
	9 40 "1" 2> /tmp/ui.dialog
	id=`cat /tmp/ui.dialog`
	dialog --inputbox "Type an aggregation time." \
	9 40 "300" 2> /tmp/ui.dialog
	if [ "$?" -eq 0 ]
	then
		sla-ng-view -m aggr -i $id -t `cat /tmp/ui.dialog`\
			 > /tmp/ui.dialog 2>&1 &
		dialog --tailbox /tmp/ui.dialog 20 75
		pkill -9 -f sla-ng-view
	fi
}

menu_ping()
{
	dialog --inputbox "Type an address to ping" \
	7 40 www.tele2.se 2> /tmp/ui.dialog
	if [ "$?" -eq 0 ]
	then
		ping -c 10 `cat /tmp/ui.dialog` > /tmp/ui.dialog 2>&1 &
		dialog --tailbox /tmp/ui.dialog 20 75
		pkill -9 ping
	fi
}

menu_shell()
{
	clear # clear screen
	echo "Starting bash..."
	reset # reset terminal; dialog may fuck it up :)
	echo "Press CTRL+d to return to the console."
	bash
}

# MAIN MENU LOOP
while true
do
	dialog  --title "SLA-NG Configuration Console" \
		--menu "Welcome! Navigate using arrow keys and TAB." \
		17 50 12 \
		n "Configure Network Interfaces" \
		d "Configure DNS" \
		t "Configure Timezone" \
		h "Configure Host Name" \
		c "Configure SLA-NG" \
		l "Show SLA-NG Log" \
		v "Show SLA-NG Pings" \
		a "Show SLA-NG Aggregate" \
		p "Ping Host" \
		s "Start Shell (bash)"  \
		2> /tmp/ui.dialog
	if [ "$?" -ne 0 ]
	then
		exit
	fi
	if [ "`cat /tmp/ui.dialog`" = "n" ]
	then
		menu_network
	fi
	if [ "`cat /tmp/ui.dialog`" = "d" ]	
	then
		menu_dns
	fi
	if [ "`cat /tmp/ui.dialog`" = "t" ]
	then
		menu_timezone
	fi
	if [ "`cat /tmp/ui.dialog`" = "h" ]
	then
		menu_hostname
	fi
	if [ "`cat /tmp/ui.dialog`" = "c" ]
	then
		menu_cfg
	fi
	if [ "`cat /tmp/ui.dialog`" = "l" ]
	then
		menu_log
	fi
	if [ "`cat /tmp/ui.dialog`" = "v" ]
	then
		menu_view
	fi
	if [ "`cat /tmp/ui.dialog`" = "a" ]
	then
		menu_aggr
	fi
	if [ "`cat /tmp/ui.dialog`" = "p" ]
	then
		menu_ping
	fi
	if [ "`cat /tmp/ui.dialog`" = "s" ]
	then
		menu_shell
	fi
done

