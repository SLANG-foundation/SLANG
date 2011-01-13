/*
 * RECEIVE PACKET
 * Author: Anders Berggren
 */

#include "probed.h"

int recv_w_ts(int sock, int flags, struct packet *pkt) {
	char data[DATALEN];
	struct sockaddr_in6 addr; /* message remote addr */
	struct msghdr msg; /* message */
	struct iovec entry; /* misc data, such as timestamp */
	struct {
		struct cmsghdr cm;
		char control[512];
	} control;

	/* prepare message structure */
	memset(&data, 0, sizeof data);
	memset(&msg, 0, sizeof msg);
	msg.msg_iov = &entry;
	msg.msg_iovlen = 1;
	entry.iov_base = data;
	entry.iov_len = sizeof data;
	msg.msg_name = (caddr_t)&addr;
	msg.msg_namelen = sizeof addr;
	msg.msg_control = &control;
	msg.msg_controllen = sizeof control;

	if (recvmsg(sock, &msg, flags) < 0) {
		if (flags & MSG_ERRQUEUE)
			syslog(LOG_INFO, "recvmsg: %s (ts)", strerror(errno));
		else
			syslog(LOG_INFO, "recvmsg: %s", strerror(errno));
		return -1;
	} else {
		if (flags & MSG_ERRQUEUE) {
			/* store kernel tx tstamp */
			if (tstamp_extract(&msg, &(pkt->ts)) < 0) return -1;
			/* tx timestamp packet, just save and bail */
			return 0;
		} else {
			/* store rx tstamp */
			if (tstamp_extract(&msg, &(pkt->ts)) < 0)
				syslog(LOG_ERR, "RX timestamp error.");
		
			pkt->addr = addr; /* save peer address */
			/* below is some debugging, delete? 
			char tmp[INET6_ADDRSTRLEN];
			inet_ntop(AF_INET6, &(them.sin6_addr), tmp, 
					INET6_ADDRSTRLEN);
			syslog(LOG_DEBUG, "* RX from: %s\n", tmp); 
			syslog(LOG_DEBUG, "* RX data: %zu bytes\n", 
						sizeof(p.data));
			syslog(LOG_DEBUG, "* RX time: %010ld.%09ld\n", 
						p.ts.tv_sec,
						p.ts.tv_nsec);*/
			return 0;
		}
	}
	return -1;
}

int send_w_ts(int sock, struct packet_p *pkt_p) {
	struct sockaddr *addr;
	int bytes, sa_len;

	/* get userland tx timestamp (before send, hehe) */
	if (cfg.ts == 'u')   
		clock_gettime(CLOCK_REALTIME, pkt_p->ts);
	/* do the send */
	bytes = strlen(pkt_p->data);
	sa_len = sizeof *(pkt_p->addr);
	addr = (struct sockaddr*)pkt_p->addr;
	if (sendto(sock, pkt_p->data, bytes, 0, addr, sa_len) < 0)
		syslog(LOG_INFO, "sendto: %s", strerror(errno));
	/* get kernel tx timestamp */
	if (cfg.ts != 'u') 
		tstamp_fetch_tx(sock, pkt_p->ts);
	return 0;
}
