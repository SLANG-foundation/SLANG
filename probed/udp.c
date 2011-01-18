/*
 * UDP PING
 * Author: Anders Berggren
 *
 * Protocol code for both client and server. 
 */

#include <errno.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <assert.h>
#include "probed.h"

void bind_or_die(/*@out@*/ int *s_udp, /*@out@*/ int *s_tcp, uint16_t port) {
	int f = 0;
	socklen_t slen;
	struct sockaddr_in6 my;

	syslog(LOG_INFO, "Binding port %d\n", (int)port);
	my.sin6_family = (sa_family_t)AF_INET6;
	my.sin6_port = htons(port);
	my.sin6_addr = in6addr_any;
	/* UDP socket */
	*s_udp = socket(PF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (*s_udp < 0) {
		syslog(LOG_ERR, "socket: %s", strerror(errno));
		exit(EXIT_FAILURE);
	} 
	/* Give us a dual-stack (ipv4/6) socket */
	slen = (socklen_t)sizeof f;
	if (setsockopt(*s_udp, IPPROTO_IPV6, IPV6_V6ONLY, &f, slen) < 0)
		syslog(LOG_ERR, "setsockopt: IPV6_V6ONLY: %s", strerror(errno));
	/* Bind port */
	slen = (socklen_t)sizeof (struct sockaddr_in6);
	if (bind(*s_udp, (struct sockaddr *)&my, slen) < 0) {
		syslog(LOG_ERR, "bind: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	/* TCP socket */
	*s_tcp = socket(PF_INET6, SOCK_STREAM, IPPROTO_TCP);
	if (*s_tcp < 0) {
		syslog(LOG_ERR, "socket: %s", strerror(errno));
		exit(EXIT_FAILURE);
	} 
	/* Give us a dual-stack (ipv4/6) socket */
	f = 0;
	slen = (socklen_t)sizeof f;
	if (setsockopt(*s_tcp, IPPROTO_IPV6, IPV6_V6ONLY, &f, slen) < 0)
		syslog(LOG_ERR, "setsockopt: IPV6_V6ONLY: %s", strerror(errno));
	f = 1;
	if (setsockopt(*s_tcp, SOL_SOCKET, SO_REUSEADDR, &f, slen) < 0)
		syslog(LOG_ERR, "setsockopt: SO_REUSEADDR: %s", strerror(errno));
	/* Bind port */
	slen = (socklen_t)sizeof (struct sockaddr_in6);
	if (bind(*s_tcp, (struct sockaddr *)&my, slen) < 0) {
		syslog(LOG_ERR, "bind: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
}

void loop_or_die(int s_udp, int s_tcp, /*@null@*/ char *addr, char *port) {
	struct addrinfo /*@dependent@*/ hints;
	struct addrinfo *ai;
	char addrstr[INET6_ADDRSTRLEN];
	struct sockaddr_in6 *their;
	struct packet pkt;
	struct packet_data tx;
	struct packet_data *rx;
	struct timespec t1;
	struct timeval tv, last, now, interval;
	uint32_t seq;
	fd_set fs;

	/* Address lookup */
	if (addr == NULL) {
		syslog(LOG_INFO, "Server mode: listening at %s\n", port);
		their = NULL;
		ai = NULL;
	} else {
		memset(&hints, 0, sizeof hints);
		hints.ai_family = AF_INET6;
		hints.ai_flags = AI_V4MAPPED;
		if (getaddrinfo(addr, port, &hints, &ai) < 0) {
			syslog(LOG_ERR, "getaddrinfo: %s", strerror(errno));
			exit(EXIT_FAILURE);
		}
		their = (struct sockaddr_in6*)ai->ai_addr;
		if (inet_ntop(AF_INET6, &(their->sin6_addr), addrstr, 
					INET6_ADDRSTRLEN) == NULL) {
			syslog(LOG_ERR, "inet_ntop: %s", strerror(errno));
			exit(EXIT_FAILURE);
		}
		syslog(LOG_INFO, "Client mode: PING to %s:%s\n", addrstr, port);
	}
	/* Timers for sending data */
	seq = 0;
	tv.tv_sec = 0;
	tv.tv_usec = 10;
	interval.tv_sec = 0;
	interval.tv_usec = 100000;
	(void)gettimeofday(&last, 0);
	while (1 == 1) {
		unix_fd_zero(&fs);
		unix_fd_set(s_udp, &fs);
		if (select(s_udp + 1, &fs, NULL, NULL, &tv) > 0) {
			pkt.data[0] = '\0';
			if (recv_w_ts(s_udp, 0, &pkt) < 0)
				syslog(LOG_ERR, "recv_w_ts: error");
			rx = (struct packet_data*)&pkt.data;
			if (pkt.data[0] == TYPE_PING) {
				syslog(LOG_INFO, "> PING %d\n", rx->seq);
				tx.type = TYPE_PONG;
				tx.seq = rx->seq;
				tx.id = 1;
				if (send_w_ts(s_udp, &(pkt.addr), (char*)&tx, &t1) < 0)
					syslog(LOG_ERR, "send_w_ts: error");
				syslog(LOG_INFO, "< PONG %d\n", tx.seq);
			} 
			if (pkt.data[0] == TYPE_PONG) {
				syslog(LOG_INFO, "> PONG %d\n", rx->seq);
			} 
			if (pkt.data[0] == TYPE_TIME) {
				syslog(LOG_INFO, "> TS %d\n", rx->seq);
			} 
		} else {
			if (their == NULL) continue;
			(void)gettimeofday(&now, 0);
			diff_tv(&tv, &now, &last);
			if (cmp_tv(&tv, &interval) == 1) {
				tx.type = TYPE_PING;
				tx.id = 1;
				tx.seq = seq;
				if (send_w_ts(s_udp, their, (char*)&tx, &t1) < 0)
					syslog(LOG_ERR, "send_w_ts: error");
				(void)gettimeofday(&last, 0);
				syslog(LOG_INFO, "< PING %d\n", (int)seq);
				seq++;
			}
		}
	}
	if (ai != NULL) freeaddrinfo(ai);
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

