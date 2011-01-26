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
#include "external/sockios.h"
#include "external/net_tstamp.h"

/*
 * Try to enable hardware timestamping, otherwise fall back to kernel.
 * LINT 'unique' is added to iface, since strncpy has undefined behaviour
 * if sharing storage with dev. 
 */
void tstamp_mode_hardware(int sock, char *iface) {
	struct ifreq dev; /* request to ioctl */
	struct hwtstamp_config hwcfg; /* hw tstamp cfg to ioctl req */
	int f = 0; /* flags to setsockopt for socket request */
	socklen_t slen;
	
	slen = (socklen_t)sizeof f;
	/* STEP 1: ENABLE HW TIMESTAMP ON IFACE IN IOCTL */
	memset(&dev, 0, sizeof(dev));
	/*@ -mayaliasunique Trust me, iface and dev doesn't share storage */
	strncpy(dev.ifr_name, iface, sizeof dev.ifr_name);
	/*@ +mayaliasunique */
	/*@ -immediatetrans Yes, we might overwrite ifr_data */
	dev.ifr_data = (void *)&hwcfg;
	memset(&hwcfg, 0, sizeof &hwcfg); 
	/*@ +immediatetrans */
	/* enable tx hw tstamp, ptp style, intel 82580 limit */
	hwcfg.tx_type = HWTSTAMP_TX_ON; 
	/* enable rx hw tstamp, all packets, yey! */
	hwcfg.rx_filter = HWTSTAMP_FILTER_ALL; 
	/* apply by sending to ioctl */
	if (ioctl(sock, SIOCSHWTSTAMP, &dev) < 0) {
		syslog(LOG_ERR, "ioctl: SIOCSHWTSTAMP: %s", strerror(errno));
		syslog(LOG_ERR, "Check %s, and that you're root\n", iface);
		/* otherwise, try kernel timestamps (socket only) */ 
		syslog(LOG_INFO, "Falling back to kernel timestamps");
		tstamp_mode_kernel(sock);
		return;
	}
	/* STEP 2: ENABLE NANOSEC TIMESTAMPING ON SOCKET */
	f |= SOF_TIMESTAMPING_TX_HARDWARE;
	f |= SOF_TIMESTAMPING_RX_HARDWARE;
	f |= SOF_TIMESTAMPING_RAW_HARDWARE;
	if (setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING, &f, slen) < 0) {
		/* bail to userland timestamps (socket only) */ 
		syslog(LOG_ERR, "SO_TIMESTAMPING: %s", strerror(errno));
		syslog(LOG_INFO, "Falling back to userland timestamps");
		tstamp_mode_userland(sock);
		return;
	}
	syslog(LOG_INFO, "Using hardware timestamps");
	cfg.ts = 'h';
}

void tstamp_mode_kernel(int sock) {
	int f = 0; /* flags to setsockopt for socket request */
	socklen_t slen;
	
	slen = (socklen_t)sizeof f;
	f |= SOF_TIMESTAMPING_TX_SOFTWARE;
	f |= SOF_TIMESTAMPING_RX_SOFTWARE;
	f |= SOF_TIMESTAMPING_SOFTWARE;
	if (setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING, &f, slen) < 0) {
		syslog(LOG_ERR, "SO_TIMESTAMPING: %s", strerror(errno));
		syslog(LOG_INFO, "Falling back to userland timestamps");
		tstamp_mode_userland(sock);
		return;
	}
	syslog(LOG_INFO, "Using kernel timestamps");
	cfg.ts = 'k';
}

void tstamp_mode_userland(sock) {
	int yes = 1;
	socklen_t slen;
	
	slen = (socklen_t)sizeof yes;
	if (setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPNS, &yes, slen) < 0)
		syslog(LOG_ERR, "SO_TIMESTAMP: %s", strerror(errno));
	syslog(LOG_INFO, "Using userland timestamps");
	cfg.ts = 'u';
}

/* 
 * Extracts the timestamp from a packet 'msg'
 */
int tstamp_extract(struct msghdr *msg, /*@out@*/ ts_t *ts) {
	struct cmsghdr *cmsg;
	struct scm_timestamping *t;
	struct timespec *ts_p;
	memset(ts, 0, sizeof (struct timespec));
	/*@ -branchstate Don't care about cmsg storage */
	for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
		if (cmsg->cmsg_level == SOL_SOCKET) {
			/* if hw/kernel timestamps, check SO_TIMESTAMPING */
			if (cfg.ts != 'u' && cmsg->cmsg_type == SO_TIMESTAMPING) {
				t = (struct scm_timestamping *)CMSG_DATA(cmsg);
				/*@ -onlytrans Please, let me copy the timestamp */
				if (cfg.ts == 'h') *ts = t->hwtimeraw;
				if (cfg.ts == 'k') *ts = t->systime;
				/*@ +onlytrans */
				return 0;
			}
			/* if software timestamps, check SO_TIMESTAMPNS */
			if (cfg.ts == 'u' && cmsg->cmsg_type == SO_TIMESTAMPNS) {
				ts_p = (struct timespec *)CMSG_DATA(cmsg);
				*ts = *ts_p;
				return 0;
			}
		}
	}
	/*@ +branchstate */
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
int tstamp_fetch_tx(int sock, /*@out@*/ ts_t *ts) {
	pkt_t pkt;
	fd_set fs;
	struct timeval tv, now, last; /* Timeouts for select and while */

	memset(ts, 0, sizeof (struct timespec));
	/* Wait for TX tstamp during at least 1 sec... */ 
	tv.tv_sec = 1; 
	tv.tv_usec = 0;
	(void)gettimeofday(&last, 0);
	(void)gettimeofday(&now, 0);
	/* ...and at most 2 sec */
	while (now.tv_sec - last.tv_sec < 2) {
		unix_fd_zero(&fs);
		unix_fd_set(sock, &fs);
		if (select(sock + 1, &fs, NULL, NULL, &tv) > 0) {
			/* We have a normal packet OR a timestamp */
			if (recv_w_ts(sock, MSG_ERRQUEUE, &pkt) == 0) {
				/*@ -dependenttrans Please, let me copy the timestamp */
				*ts = pkt.ts;
				/*@ +dependenttrans */
				return 0;
			}
		}
		(void)gettimeofday(&now, 0);
	}
	return -1;
}
