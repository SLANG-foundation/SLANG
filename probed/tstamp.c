/*
 * TIMESTAMPING API 
 * Author: Anders Berggren
 *
 * - Hardware timestamps require SO_TIMESTAMPING on socket and ioctl on device.
 * - Kernel   timestamps require SO_TIMESTAMPING on socket.
 * - Userland timestamps require SO_TIMESTAMPNS  on socket. 
 *
 * Function tstamp_hw()
 * Contains commands for activating hardware timestamping on both socket "s"
 * and on network interface "iface". Has to be run synchroniously.
 * 
 * Function tstamp_kernel()
 * Enable kernel timestamping, disable hardware timestamping.
 * 
 * Function tstamp_userland()
 * Enable software timestamping, disable kernel and hardware timestamping.
 * 
 * Function tstamp_get(msg)
 * Extracts the timestamp from a message.
 */ 

#include "probed.h"
#include <string.h>
#include <errno.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include "sockios.h"
#include "net_tstamp.h"

void tstamp_mode_hardware(int sock, char *iface) {
	struct ifreq dev; /* request to ioctl */
	struct hwtstamp_config hwcfg; /* hw tstamp cfg to ioctl req */
	int f = 0; /* flags to setsockopt for socket request */
	
	/* STEP 1: ENABLE HW TIMESTAMP ON IFACE IN IOCTL */
	memset(&dev, 0, sizeof(dev));
	/* get interface by iface name */
	strncpy(dev.ifr_name, iface, sizeof dev.ifr_name);
	/* now that we have ioctl, check for ip address :) */
	if (ioctl(sock, SIOCGIFADDR, &dev) < 0)
		syslog(LOG_ERR, "SIOCGIFADDR: no IP: %s", strerror(errno));
	/* point ioctl req data at hw tstamp cfg, reset tstamp cfg */
	dev.ifr_data = (void *)&hwcfg;
	memset(&hwcfg, 0, sizeof &hwcfg); 
	/* enable tx hw tstamp, ptp style, intel 82580 limit */
	hwcfg.tx_type = HWTSTAMP_TX_ON; 
	/* enable rx hw tstamp, all packets, yey! */
	hwcfg.rx_filter = HWTSTAMP_FILTER_ALL; 
	/* apply by sending to ioctl */
	if (ioctl(sock, SIOCSHWTSTAMP, &dev) < 0) {
		syslog(LOG_ERR, "ioctl: SIOCSHWTSTAMP: %s", strerror(errno));
		syslog(LOG_ERR, "Check %s, and that you're root.\n", iface);
		/* otherwise, try kernel timestamps (socket only) */ 
		syslog(LOG_INFO, "Falling back to kernel timestamps.");
		tstamp_mode_kernel(sock);
		return;
	}
	/* STEP 2: ENABLE NANOSEC TIMESTAMPING ON SOCKET */
	f |= SOF_TIMESTAMPING_TX_HARDWARE;
	f |= SOF_TIMESTAMPING_RX_HARDWARE;
	f |= SOF_TIMESTAMPING_RAW_HARDWARE;
	if (setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING, &f, sizeof f) < 0) {
		/* bail to userland timestamps (socket only) */ 
		syslog(LOG_ERR, "SO_TIMESTAMPING: %s", strerror(errno));
		syslog(LOG_INFO, "Falling back to userland timestamps.");
		tstamp_mode_userland(sock);
		return;
	}
	syslog(LOG_INFO, "Using hardware timestamps.");
	cfg.ts = 'h';
}

void tstamp_mode_kernel(int sock) {
	int f = 0; /* flags to setsockopt for socket request */

	f |= SOF_TIMESTAMPING_TX_SOFTWARE;
	f |= SOF_TIMESTAMPING_RX_SOFTWARE;
	f |= SOF_TIMESTAMPING_SOFTWARE;
	if (setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING, &f, sizeof f) < 0) {
		syslog(LOG_ERR, "SO_TIMESTAMPING: %s", strerror(errno));
		syslog(LOG_INFO, "Falling back to userland timestamps.");
		tstamp_mode_userland(sock);
		return;
	}
	syslog(LOG_INFO, "Using kernel timestamps.");
	cfg.ts = 'k';
}

void tstamp_mode_userland(sock) {
	int yes = 1;

	if (setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPNS, &yes, sizeof yes) < 0)
		syslog(LOG_ERR, "SO_TIMESTAMP: %s", strerror(errno));
	syslog(LOG_INFO, "Using software timestamps.");
	cfg.ts = 'u';
}

/* 
 * Extracts the timestamp from a packet 'msg'
 */
int tstamp_extract(struct msghdr *msg, struct timespec *ts) {
	struct cmsghdr *cmsg;
	struct scm_timestamping *t;
	struct timespec *ts_p;
	for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
		if (cmsg->cmsg_level == SOL_SOCKET) {
			/* if hw/kernel timestamps, check SO_TIMESTAMPING */
			if (cfg.ts != 'u' && cmsg->cmsg_type == SO_TIMESTAMPING) {
				t = (struct scm_timestamping *)CMSG_DATA(cmsg);
				if (cfg.ts == 'h') *ts = t->hwtimeraw;
				if (cfg.ts == 'k') *ts = t->systime;
				return 0;
			}
			/* if software timestamps, check SO_TIMESTAMPNS */
			if (cfg.ts == 's' && cmsg->cmsg_type == SO_TIMESTAMPNS) {
				ts_p = (struct timespec *)CMSG_DATA(cmsg);
				*ts = *ts_p;
				return 0;
			}
		}
	}
	return -1;
}

/*
 * Function that waits for a TX timestamp on the error queue, used for 
 * the kernel (and hardware) timestamping mode. It does so by select()ing 
 * reads, and checking for approx 1 second. Note that normal packets will
 * trigger the select() as well, therefore the while-loop is needed. 
 * As usual, the timestamp is stored by data_recv() calling tstamp_get()
 * in the global variable ts.
 */
int tstamp_fetch_tx(int sock, struct timespec *ts) {
	struct packet pkt;
	fd_set fs; /* select fd set for hw tstamps */
	struct timeval tv, now, last; /* timeout for select and while */

	/* wait for tx tstamp during at least 1 sec... */ 
	tv.tv_sec = 1; 
	tv.tv_usec = 0;
	gettimeofday(&last, 0);
	gettimeofday(&now, 0);
	/* ...and at most max 2 sec */
	while (now.tv_sec - last.tv_sec < 2) {
		FD_ZERO(&fs);
		FD_SET(sock, &fs);
		if (select(sock + 1, &fs, 0, 0, &tv) > 0) {
			/* we have normal packet or timestamp */
			if (recv_w_ts(sock, MSG_ERRQUEUE, &pkt) == 0) {
				/* we have timestamp */
				*ts = pkt.ts;
				return 0;
			}
		}
		gettimeofday(&now, 0);
	}
	syslog(LOG_ERR, "Kernel TX timestamp error.");
	ts->tv_sec = 0;
	ts->tv_nsec = 0;
	return -1;
}
