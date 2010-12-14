/*
 * RECEIVE PACKET
 * Author: Anders Berggren
 *
 * Function recv_
 */

#include "sla-ng.h"

struct packet p;

void data_recv(int flags) {
	struct msghdr msg; /* message */
	struct iovec entry; /* misc data, such as timestamp */
	struct sockaddr_in addr; /* message remote addr */
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
		if (flags & MSG_ERRQUEUE)
			perror("recvmsg: tstamp");
		/*else
			perror("recvmsg: data");*/
		return;
	} else {
		tstamp_get(&msg); /* store kernel/hw rx/tx tstamp */
		if (flags & MSG_ERRQUEUE) {
			return;
		} else {
			them = addr;
			if (c.debug) {
				printf("* RX from: %s\n", 
						inet_ntoa(them.sin_addr));
				printf("* RX data: %d bytes\n", 
						(int)sizeof(p.data));
				printf("* RX time: %010ld.%09ld\n", 
						p.ts.tv_sec, p.ts.tv_nsec);
			}	
		}
	}
}

void data_send(char *d, int size) {
	fd_set fs; /* select fd set for hw tstamps */
	struct timeval tv; /* select timeout for hw tstamps */

	if (c.debug) printf("* TX to: %s\n", inet_ntoa(them.sin_addr));
	if (c.ts == 's') clock_gettime(CLOCK_REALTIME, &p.ts); /* get sw tx */
	if (sendto(s, d, size, 0, (struct sockaddr*)&them, slen) < 0)
		perror("sendto");
	if (c.ts != 's') { /* get kernel/hw tx */
		tv.tv_sec = 1; /* wait for nic tx tstamp during 1sec */ 
		tv.tv_usec = 0;
		FD_ZERO(&fs);
		FD_SET(s, &fs);
		if (select(s + 1, &fs, 0, 0, &tv) > 0) {
			if (FD_ISSET(s, &fs))
				data_recv(MSG_ERRQUEUE); /* get tx kernel ts */
		} else {
			clock_gettime(CLOCK_REALTIME, &p.ts); /* get tx sw */
			puts("Hardware TX timestamp error, using clock");
		}
	}
	if (c.debug) printf("* TX time: %010ld.%09ld\n", 
			p.ts.tv_sec, p.ts.tv_nsec);
}
