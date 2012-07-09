/* 
 * Copyright (c) 2011 Anders Berggren, Lukas Garberg, Tele2
 *
 * We have not yet decided upon a license, and so far it may only be
 * used and redistributed with our explicit permission.
 */ 

/**
 * \file   tstamp.c
 * \brief  Timestamp init (ioctl) and extraction (MSG_ERRQUEUE and CMSG)
 * \author Anders Berggren <anders@halon.se>
 * \author Lukas Garberg <lukas@spritelink.net>
 * \date   2010-11-05
 *
 * - Hardware timestamps require SO_TIMESTAMPING on socket and ioctl on device.
 * - Kernel   timestamps require SO_TIMESTAMPING on socket and cool drivers.
 * - Userland timestamps require SO_TIMESTAMPNS  on socket. 
 */ 

#include <string.h>
#include <errno.h>
#include <syslog.h>
#ifndef S_SPLINT_S /* SPlint 3.1.2 bug */
#include <unistd.h>
#endif
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <linux/errqueue.h>
#include "external/sockios.h"
#include "external/net_tstamp.h"
#include "probed.h"
#include "tstamp.h"
#include "unix.h"
#include "net.h"

#ifndef SO_TIMESTAMPING
#define SO_TIMESTAMPING 37
#define SCM_TIMESTAMPING SO_TIMESTAMPING
#endif

struct scm_timestamping {
        struct timespec systime;
        struct timespec hwtimesys;
        struct timespec hwtimeraw;
};

/**
 * Try to enable hardware timestamping, otherwise fall back to kernel.
 *
 * Run ioctl() on 'iface' (supports Intel 82580 right now) and run
 * setsockopt SO_TIMESTAMPING with RAW_HARDWARE on socket 'sock'. 
 * 
 * \param[in] sock  The socket to activate SO_TIMESTAMPING on (the UDP)
 * \param[in] iface The interface name to ioctl SIOCSHWTSTAMP on
 * \warning         Requires CONFIG_NETWORK_PHY_TIMESTAMPING Linux option
 * \warning         Supports only Intel 82580 right now
 * \bug             Intel 82580 has bugs such as IPv4 only, and RX issues
 */
void tstamp_mode_hardware(int sock, char *iface) {
	struct ifreq dev; /* request to ioctl */
	struct hwtstamp_config hwcfg; /* hw tstamp cfg to ioctl req */
	int f = 0; /* flags to setsockopt for socket request */
	socklen_t slen;
	
	slen = (socklen_t)sizeof f;
	/* STEP 1: ENABLE HW TIMESTAMP ON IFACE IN IOCTL */
	memset(&dev, 0, sizeof dev);
	/*@ -mayaliasunique Trust me, iface and dev doesn't share storage */
	strncpy(dev.ifr_name, iface, sizeof dev.ifr_name);
	/*@ +mayaliasunique */
	/*@ -immediatetrans Yes, we might overwrite ifr_data */
	dev.ifr_data = (void *)&hwcfg;
	memset(&hwcfg, 0, sizeof hwcfg); 
	/*@ +immediatetrans */
	/* enable tx hw tstamp, ptp style, intel 82580 limit */
	hwcfg.tx_type = HWTSTAMP_TX_ON; 
	/* enable rx hw tstamp, all packets, yey! */
	hwcfg.rx_filter = HWTSTAMP_FILTER_ALL;
   	/* Check that one is root */
	if (getuid() != 0)
		syslog(LOG_ERR, "Hardware timestamps requires root privileges");
	/* apply by sending to ioctl */
	if (ioctl(sock, SIOCSHWTSTAMP, &dev) < 0) {
		syslog(LOG_ERR, "ioctl: SIOCSHWTSTAMP: %s", strerror(errno));
		syslog(LOG_ERR, "Verify that %s supports hardware timestamp\n",
			       iface);
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
	cfg.ts = HARDWARE;
}

/**
 * Try to enable kernel timestamping, otherwise fall back to software.
 *
 * Run setsockopt SO_TIMESTAMPING with SOFTWARE on socket 'sock'. 
 * 
 * \param[in] sock  The socket to activate SO_TIMESTAMPING on (the UDP)
 * \warning         Requires CONFIG_NETWORK_PHY_TIMESTAMPING Linux option
 * \warning         Requires drivers fixed with skb_tx_timestamp() 
 */
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
	cfg.ts = KERNEL;
}

/**
 * Enable software timestamping, usually as last resort.
 *
 * Run setsockopt SO_TIMESTAMPNS on socket 'sock'. 
 * 
 * \param[in] sock  The socket to activate SO_TIMESTAMPING on (the UDP)
 * \warning         Really sucks in terms of accuracy! Use kernel/hardware
 */
void tstamp_mode_userland(sock) {
	int yes = 1;
	socklen_t slen;
	
	slen = (socklen_t)sizeof yes;
	if (setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPNS, &yes, slen) < 0)
		syslog(LOG_ERR, "SO_TIMESTAMP: %s", strerror(errno));
	syslog(LOG_INFO, "Using userland timestamps");
	cfg.ts = USERLAND;
}

/**
 * Extracts the timestamp from a packet 'msg's CMSG data
 *
 * Normally invoked only by recv_w_ts(), and indirectly by send_w_ts() via
 * the function tstamp_fetch_tx(). Depending on timestamp method (defined
 * in the global variable cfg's 'ts' field) fetch the correct CMSG field
 * and store it's timestamp.
 * 
 * \param[in]  msg Pointer to the message's header data
 * \param[out] ts  Pointer to location where timestamp is saved
 */
int tstamp_extract(struct msghdr *msg, /*@out@*/ ts_t *ts, int tx) {
	struct cmsghdr *cmsg;
	struct scm_timestamping *t;
	ts_t *ts_p;
	int ok = 0;
	struct sock_extended_err *err;

	/* Check message headers */
	memset(ts, 0, sizeof *ts);
	/*@ -branchstate Don't care about cmsg storage */
	for (cmsg = CMSG_FIRSTHDR(msg); cmsg; cmsg = CMSG_NXTHDR(msg, cmsg)) {
		if (cmsg->cmsg_level == SOL_SOCKET) {
			/* if hw/kernel timestamps, check SO_TIMESTAMPING */
			if (cfg.ts != USERLAND &&
					cmsg->cmsg_type == SO_TIMESTAMPING) {
				t = (struct scm_timestamping *)CMSG_DATA(cmsg);
				/*@ -onlytrans Let me copy the timestamp */
				if (cfg.ts == HARDWARE) *ts = t->hwtimeraw;
				if (cfg.ts == KERNEL) *ts = t->systime;
				/*@ +onlytrans */
				if (!tx) return 0;
				else ok |= 1;
				if (ok == 3) return 0;
			}
			/* if RX software timestamps, check SO_TIMESTAMPNS */
			if (cfg.ts == USERLAND
					&& cmsg->cmsg_type == SO_TIMESTAMPNS) {
				ts_p = (struct timespec *)CMSG_DATA(cmsg);
				*ts = *ts_p;
				return 0;
			}
		}
		/* Check that this is the right packet */
		if (tx && cmsg->cmsg_level == IPPROTO_IPV6) {
			if (cmsg->cmsg_type == IPV6_RECVERR) {
				err = (struct sock_extended_err
					 *)CMSG_DATA(cmsg);
				if (err->ee_origin ==
						SO_EE_ORIGIN_TIMESTAMPING) {
					ok |= 2;
					if (ok == 3) return 0;
				}
			}
		}
	}
	/*@ +branchstate */
	return -1;
}

/**
 * Fetch a TX timestamp, should be executed right after send()
 *
 * Function that waits for a TX timestamp on the error queue, used for 
 * the kernel (and hardware) timestamping mode. It does so by select()ing 
 * reads, and checking for approx 1 second. Note that normal packets will
 * trigger the select() as well, therefore the while-loop is needed. 
 * As usual, the timestamp is stored by data_recv() calling tstamp_get()
 * in the global variable ts.
 *
 * \param[in]  sock Socket to read timestamp from
 * \param[out] Pointer to timestamp, where the TX timestamp is saved
 * \bug        Waits (selects) during 1-2 seconds, blocking, which is bad
 */
int tstamp_fetch_tx(int sock, /*@out@*/ ts_t *ts) {
	pkt_t pkt;
	fd_set fs;
	struct timeval tv, now, last, tmp; /* Timeouts for select and while */

	memset(ts, 0, sizeof *ts);
	/* Wait for TX tstamp */
	(void)gettimeofday(&last, 0);
	(void)gettimeofday(&now, 0);
	tv.tv_sec = 0;
	tv.tv_usec = 10000;
	timersub(&now, &last, &tmp);
	while (timercmp(&tmp, &tv, <) != 0) {
		unix_fd_zero(&fs);
		unix_fd_set(sock, &fs);
		if (select(sock + 1, &fs, NULL, NULL, &tv) > 0) {
			/* We have a normal packet OR a timestamp */
			if (recv_w_ts(sock, MSG_ERRQUEUE, &pkt) == 0) {
				/*@ -dependenttrans Let me copy timestamp */
				*ts = pkt.ts;
				/*@ +dependenttrans */
				return 0;
			}
		}
		tv.tv_sec = 0;
		tv.tv_usec = 10000;
		(void)gettimeofday(&now, 0);
		timersub(&now, &last, &tmp);
	}
	return -1;
}
