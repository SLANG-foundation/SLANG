/*
 * UDP PING
 * Author: Anders Berggren
 *
 * Protocol code for both client and server. 
 */

#include "probed.h"
#include <netdb.h>

int udp_bind(int *sock, int port) {
	int no = 0;
	int sa_len;
	struct sockaddr_in6 my;

	sa_len = sizeof(struct sockaddr_in6);
	*sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (*sock < 0) {
		syslog(LOG_ERR, "socket: %s", strerror(errno));
		exit(EXIT_FAILURE);
	} 
	/* give us a dual-stack (ipv4/6) socket */
	if (setsockopt(*sock, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof no) < 0)
		syslog(LOG_ERR, "setsockopt: IPV6_V6ONLY: %s", strerror(errno));
	/* bind port */
	syslog(LOG_INFO, "Binding port %d\n", port);
	my.sin6_family = AF_INET6;
	my.sin6_addr = in6addr_any;
	my.sin6_port = htons(port);
	if (bind(*sock, (struct sockaddr *)&my, sa_len) < 0) {
		syslog(LOG_ERR, "bind: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	return 0;
}

int udp_client(int sock, char *addr, char *port) {
	struct addrinfo hints, *ai;
	char addrstr[INET6_ADDRSTRLEN];
	struct sockaddr_in6 *their;
	struct packet pkt;
	struct packet_ping ping;
	struct packet_ping *pong;
	struct timespec tx;
	struct timeval tv, last, now, interval;
	int seq;
	fd_set fs;
				struct packet_p pkt_p;

	printf("Client mode; Sending PING to %s:%s\n", addr, port);
	/* address lookup */
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET6;
	//hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_V4MAPPED;
	if (getaddrinfo(addr, port, &hints, &ai) < 0) {
		syslog(LOG_ERR, "getaddrinfo: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	their = (struct sockaddr_in6*)ai->ai_addr;
	inet_ntop(AF_INET6, &(their->sin6_addr), addrstr, INET6_ADDRSTRLEN);
	printf("WEE: %s\n", addrstr);

	seq = 0;
	tv.tv_sec = 0;
	tv.tv_usec = 10;
	interval.tv_sec = 0;
	interval.tv_usec = 100000;
	gettimeofday(&last, 0);
	while (1) {
		FD_ZERO(&fs);
		FD_SET(sock, &fs);
		if (select(sock + 1, &fs, 0, 0, &tv) > 0) {
			pkt.data[0] = 0;
			recv_w_ts(sock, 0, &pkt);
			pong = (struct packet_ping*)&pkt.data;
			if (pkt.data[0] == TYPE_PONG) {
				syslog(LOG_DEBUG, "* PONG %d\n", pong->seq);
			} 
			if (pkt.data[0] == TYPE_TIME) {
				syslog(LOG_DEBUG, "* TS %d\n", pong->seq);
			} 
		} else {
			gettimeofday(&now, 0);
			diff_tv(&tv, &now, &last);
			if (cmp_tv(&tv, &interval) == 1) {
				ping.type = TYPE_PING;
				ping.seq = seq;
				pkt_p.data = (char*)&ping;
				pkt_p.addr = their;
				send_w_ts(sock, &pkt_p);
				gettimeofday(&last, 0);
				printf("ping %d\n", seq);
				seq++;
			}
		}
	}
}
/*void proto_timestamp() {
	struct packet_time *pt;
	struct timespec di, di2;
	int rtt;

	pt = (struct packet_time*)&p.data;
	diff_ts(&di, &(pt->tx1), &(pt->rx1));
	syslog(LOG_DEBUG, "* TIME %d %09ld\n", pt->seq1, di.tv_nsec);
	
	diff_ts(&di2, &rxglob, &txglob);
	if (c.debug) printf("DI %010ld.%09ld\n", di2.tv_sec, di2.tv_nsec);
	rtt = di2.tv_nsec - di.tv_nsec;
	printf("RT %d %d\n", rtt, pt->seq1); 

}*/

