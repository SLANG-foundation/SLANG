/*
 * RECEIVE PACKET
 * Author: Anders Berggren
 */

#include "probed.h"

struct packet p;
int cpfisk = 0;
int recv_with_ts(int flags, struct timespec *ts) {
	struct msghdr msg; /* message */
	struct iovec entry; /* misc data, such as timestamp */
	struct sockaddr_in6 addr; /* message remote addr */
	struct {
		struct cmsghdr cm;
		char control[512];
	} control;

	/* prepare message structure */
	memset(&p.data, 0, sizeof(p.data));
	memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &entry;
	msg.msg_iovlen = 1;
	entry.iov_base = p.data;
	entry.iov_len = sizeof(p.data);
	msg.msg_name = (caddr_t)&addr;
	msg.msg_namelen = sizeof(addr);
	msg.msg_control = &control;
	msg.msg_controllen = sizeof(control);

	if (recvmsg(s, &msg, flags) < 0) {
		/* evil timestamp error! */
		//if (flags & MSG_ERRQUEUE)
		//	syslog(LOG_INFO, "recvmsg: %s (ts)", strerror(errno));
		/* otherwise, normal data timeout error */
		return -1;
	} else {
		if (flags & MSG_ERRQUEUE) {
			/* store kernel tx tstamp */
			if (tstamp_get(&msg) < 0) return -1;
			/* tx timestamp packet, just save and bail */
			return 0;
		} else {
			/* store rx tstamp */
			if (tstamp_get(&msg) < 0) { 
				syslog(LOG_ERR, "RX timestamp error.");
				cpfisk = 1;
			}	
			them = addr; /* save latest peer spoken to */
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

void data_send(char *d, int size) {
	/* debugging, delete 
	char tmp[INET6_ADDRSTRLEN];
	inet_ntop(AF_INET6, &(them.sin6_addr), tmp, INET6_ADDRSTRLEN);
	syslog(LOG_DEBUG, "* TX to: %s\n", tmp); */

	if (c.ts == 's') /* get sw tx timestamp (before send hehe) */  
		clock_gettime(CLOCK_REALTIME, &p.ts);
	if (sendto(s, d, size, 0, (struct sockaddr*)&them, slen) < 0)
		syslog(LOG_INFO, "sendto: %s", strerror(errno));
	if (c.ts != 's') /* get kernel tx timestamp */
		tstamp_recv();

	/* debugging, delete 
	syslog(LOG_DEBUG, "* TX time: %010ld.%09ld\n", 
			p.ts.tv_sec, p.ts.tv_nsec);*/
}
