/*
 * HARDWARE TIMESTAMPING ACTIVATION
 * Author: Anders Berggren
 *
 * Function tstamp_hw()
 * Contains commands for activating hardware timestamping on both socket "s"
 * and on network interface "iface". Has to be run synchroniously.
 * - Hardware timestamps require SO_TIMESTAMPING on socket and ioctl on device.
 * - Kernel   timestamps require SO_TIMESTAMPING on socket.
 * - Software timestamps require SO_TIMESTAMPNS on socket. 
 * 
 * Function tstamp_sw()
 * Enable software timestamping, disable kernel and hardware timestamping.
 * 
 * Function tstamp_get(msg)
 * Extracts the timestamp from a message.
 */ 

#include "sla-ng.h"

void tstamp_hw() {
	struct ifreq dev; /* request to ioctl */
	struct hwtstamp_config hwcfg; /* hw tstamp cfg to ioctl req */
	int f = 0; /* flags to setsockopt for socket request */
	
	if (c.ts == 'h') return; /* check if it has already been run */
	/* STEP 1: ENABLE HW TIMESTAMP ON IFACE IN IOCTL */
	memset(&dev, 0, sizeof(dev));
	/* get interface by iface name */
	strncpy(dev.ifr_name, c.iface, sizeof(dev.ifr_name));
	/* now that we have ioctl, check for ip address :) */
	if (ioctl(s, SIOCGIFADDR, &dev) < 0)
		perror("ioctl: SIOCGIFADDR: No IP configured");
	/* point ioctl req data at hw tstamp cfg, reset tstamp cfg */
	dev.ifr_data = (void *)&hwcfg;
	memset(&hwcfg, 0, sizeof(&hwcfg)); 
	/* enable tx hw tstamp, ptp style, i82580 limit */
	hwcfg.tx_type = HWTSTAMP_TX_ON; 
	/* enable rx hw tstamp, all packets, yey! */
	hwcfg.rx_filter = HWTSTAMP_FILTER_ALL; 
	/* apply by sending to ioctl */
	if (ioctl(s, SIOCSHWTSTAMP, &dev) < 0) {
		perror("ioctl: SIOCSHWTSTAMP");
		printf("Check your NIC %s, and that you're root.\n", c.iface);
		/* otherwise, try kernel timestamps (socket only) */ 
		puts("Falling back to kernel timestamps.");
		tstamp_kernel();
		return;
	}
	/* STEP 2: ENABLE NANOSEC TIMESTAMPING ON SOCKET */
	f |= SOF_TIMESTAMPING_TX_HARDWARE;
	f |= SOF_TIMESTAMPING_RX_HARDWARE;
	f |= SOF_TIMESTAMPING_RAW_HARDWARE;
	if (setsockopt(s, SOL_SOCKET, SO_TIMESTAMPING, &f, sizeof(f)) < 0) {
		/* try software timestamps (socket only) */ 
		perror("setsockopt: SO_TIMESTAMPING");
		puts("Falling back to software timestamps.");
		tstamp_sw();
		return;
	}
	puts("Using hardware timestamps.");
	c.ts = 'h'; /* remember hw is used */
}

void tstamp_kernel() {
	int f = 0; /* flags to setsockopt for socket request */

	if (c.ts == 'k') return; /* check if it has already been run */
	f |= SOF_TIMESTAMPING_TX_SOFTWARE;
	f |= SOF_TIMESTAMPING_RX_SOFTWARE;
	f |= SOF_TIMESTAMPING_SOFTWARE;
	if (setsockopt(s, SOL_SOCKET, SO_TIMESTAMPING, &f, sizeof(f)) < 0) {
		perror("setsockopt: SO_TIMESTAMPING");
		puts("Falling back to software timestamps.");
		tstamp_sw();
		return;
	}
	puts("Using kernel timestamps.");
	c.ts = 'k'; /* remember kernel is used */
}

void tstamp_sw() {
	if (c.ts == 's') return; /* check if it has already been run */
	if (setsockopt(s, SOL_SOCKET, SO_TIMESTAMPNS, &yes, sizeof(yes)) < 0)
		perror("setsockopt: SO_TIMESTAMP");
	puts("Using software timestamps.");
	c.ts = 's';
}

void tstamp_get(struct msghdr *msg) {
	struct cmsghdr *cmsg;
	struct scm_timestamping *t;
	struct timespec *t2;

	for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
		if (cmsg->cmsg_level == SOL_SOCKET) {
			/* if hw/kernel timestamps, check SO_TIMESTAMPING */
			if (c.ts != 's' && cmsg->cmsg_type == SO_TIMESTAMPING) {
				t = (struct scm_timestamping *)CMSG_DATA(cmsg);
				if (c.ts == 'h') p.ts = t->hwtimeraw;
				if (c.ts == 'k') p.ts = t->systime;
				return;
			}
			/* if software timestamps, check SO_TIMESTAMPNS */
			if (c.ts == 's' && cmsg->cmsg_type == SO_TIMESTAMPNS) {
				t2 = (struct timespec *)CMSG_DATA(cmsg);
				p.ts = *t2;
				return;	
			}
		}
	}
}
