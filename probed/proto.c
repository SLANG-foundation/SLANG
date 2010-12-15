/*
 * UDP PING
 * Author: Anders Berggren
 *
 * Protocol code for both client and server. 
 */

#include "sla-ng.h"

struct sockaddr_in my;
struct timespec rxglob, txglob;

void proto() {
	struct packet_ping pp;
	struct timespec tx;
	struct timeval tv, last, now;
	int seq, r;
	char tmp[48];
	
	seq = 0;
	tv.tv_sec = 0;
	tv.tv_usec = 1;
	if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
		syslog(LOG_ERR, "setsockopt: SO_RCVTIMEO: %s", strerror(errno));
	
	gettimeofday(&last, 0);
	while (1) {
		p.data[0] = 0;
		data_recv(0); /* wait for ping/pong/ts */
		if (p.data[0] == TYPE_PONG) proto_client();
		if (p.data[0] == TYPE_PING) proto_server();
		if (p.data[0] == TYPE_TIME) proto_timestamp();
		/* send ping (client) */
		gettimeofday(&now, 0);
		diff_tv(&tv, &now, &last);
		if (tv.tv_sec >= 0 && tv.tv_usec >= 100000) {
			r = config_getkey("/config/probe[1]/address", tmp, 48);
			if (r < 0) continue;
			them.sin6_family = AF_INET6;
			them.sin6_port = htons(c.port);
			if (inet_pton(AF_INET6, tmp, &them.sin6_addr) < 0)
				syslog(LOG_INFO, "pton: %s", strerror(errno));
			pp.type = TYPE_PING;
			pp.seq = seq;
			data_send((char*)&pp, sizeof(pp));
			tx = p.ts; /* save timestamp */
			txglob = p.ts; /* save timestamp */
			seq++;
			gettimeofday(&last, 0);
		}
	}
}

void proto_server() {
	struct packet_ping *pp;
	struct packet_time pt;
	struct timespec rx, tx, di;

	rx = p.ts; /* save timestamp */
	pp = (struct packet_ping*)&p.data;
	pp->type = TYPE_PONG;
	syslog(LOG_DEBUG, "* PING %d\n", pp->seq);
	data_send((char*)pp, sizeof(struct packet_ping)); /* send pong */
	tx = p.ts; /* save timestamp */

	diff_ts(&di, &tx, &rx);
	syslog(LOG_DEBUG, "DI %010ld.%09ld\n", di.tv_sec, di.tv_nsec);
	pt.type = TYPE_TIME;
	pt.seq1 = pp->seq;
	pt.rx1 = rx;
	pt.tx1 = tx;
	data_send((char*)&pt, sizeof(pt)); /* send diff */
}

void proto_client() {
	struct packet_ping *pp;
	struct timespec rx;

	rx = p.ts; /* save timestamp */
	rxglob = p.ts; /* save timestamp */
	pp = (struct packet_ping*)&p.data;
	syslog(LOG_DEBUG, "* PONG %d\n", pp->seq);
}

void proto_timestamp() {
	struct packet_time *pt;
	struct timespec di, di2;
	int rtt;

	pt = (struct packet_time*)&p.data;
	diff_ts(&di, &(pt->tx1), &(pt->rx1));
	syslog(LOG_DEBUG, "* TIME %d %09ld\n", pt->seq1, di.tv_nsec);
	
	diff_ts(&di2, &rxglob, &txglob);
	if (c.debug) printf("DI %010ld.%09ld\n", di2.tv_sec, di2.tv_nsec);
	rtt = di2.tv_nsec - di.tv_nsec;
	printf("RT %d\n", rtt); 

}

void proto_bind(int port) {
	if (port == c.port) return;
	syslog(LOG_INFO, "Binding port %d\n", port);
	c.port = port;
	my.sin_family = AF_INET6;
	my.sin_addr.s_addr = htonl(INADDR_ANY);
	my.sin_port = htons(port);
	if (bind(s, (struct sockaddr *)&my, slen) < 0) 
		syslog(LOG_ERR, "bind: %s", strerror(errno));
}
