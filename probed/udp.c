/*
 * UDP PING
 * Author: Anders Berggren
 *
 * Protocol code for both client and server. 
 */

#include <errno.h>
#include <netdb.h>
#include <string.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/time.h>
#ifndef S_SPLINT_S /* SPlint 3.1.2 bug */
#include <unistd.h>
#endif
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
	if (listen(*s_tcp, 10) == -1) {
		syslog(LOG_ERR, "listen: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
}

void loop_or_die(int s_udp, int s_tcp, /*@null@*/ char *addr, char *port) {
	struct addrinfo /*@dependent@*/ hints;
	struct addrinfo *ai;
	char addrstr[INET6_ADDRSTRLEN];
	struct sockaddr_in6 *their, addr_tmp;
	struct packet pkt;
	struct packet_data tx;
	struct packet_data *rx;
	struct timespec t1;
	struct timeval tv, last, now, interval;
	uint32_t seq;
	fd_set fs, fs_tmp;
	int fd, fd_max = 0, fd_first, r;
	int fd_pipe[2];
	socklen_t slen;

	/* IPC for children-to-parent (TCP client to UDP state machine) */
	if (pipe(fd_pipe) < 0) {
		syslog(LOG_ERR, "pipe: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	/* Address lookup */
	if (addr == NULL) {
		syslog(LOG_INFO, "Server mode: listening at %s\n", port);
		their = NULL;
		ai = NULL;
	} else {
		fork_client(fd_pipe[1], addr, port);
		memset(&hints, 0, sizeof hints);
		hints.ai_family = AF_INET6;
		hints.ai_flags = AI_V4MAPPED;
		r = getaddrinfo(addr, port, &hints, &ai);
		if (r < 0) {
			syslog(LOG_ERR, "getaddrinfo: %s", gai_strerror(r));
			exit(EXIT_FAILURE);
		}
		their = (struct sockaddr_in6*)ai->ai_addr;
		if (addr2str(their, addrstr) < 0) 
			exit(EXIT_FAILURE);
		syslog(LOG_INFO, "Client mode: PING to %s:%s\n", addrstr, port);
	}
	/* Timers for sending data */
	seq = 0;
	interval.tv_sec = 0;
	interval.tv_usec = 100000;
	(void)gettimeofday(&last, 0);
	/* Add both pipe, UDP and TCP to the FD set, note highest FD */
	unix_fd_zero(&fs);
	unix_fd_zero(&fs_tmp);
	unix_fd_set(s_udp, &fs);
	unix_fd_set(s_tcp, &fs);
	unix_fd_set(s_tcp, &fs);
	unix_fd_set(fd_pipe[0], &fs);
	if (s_udp      > fd_max) fd_max = s_udp;
	if (s_tcp      > fd_max) fd_max = s_tcp;
	if (fd_pipe[0] > fd_max) fd_max = fd_pipe[0];
	if (fd_pipe[1] > fd_max) fd_max = fd_pipe[1];
	fd_first = fd_max;
	/* Let's loop those sockets! */
	while (1 == 1) {
		fs_tmp = fs;
		tv.tv_sec = 0;
		tv.tv_usec = 1000000;
		if (select(fd_max + 1, &fs_tmp, NULL, NULL, &tv) > 0) {
			if (unix_fd_isset(s_udp, &fs_tmp) == 1) {
				/* UDP socket; PING and PONG */
				pkt.data[0] = '\0';
				if (recv_w_ts(s_udp, 0, &pkt) < 0)
					syslog(LOG_ERR, "recv_w_ts: error");
				rx = (struct packet_data *)&pkt.data;
				if (pkt.data[0] == TYPE_PING) {
					syslog(LOG_INFO, "> PING %d\n", rx->seq);
					tx.type = TYPE_PONG;
					tx.seq = rx->seq;
					tx.id = 1;
					if (send_w_ts(s_udp, &(pkt.addr), (char*)&tx, &t1) < 0)
						syslog(LOG_ERR, "send_w_ts: error");
					syslog(LOG_INFO, "< PONG %d\n", tx.seq);
					fd = tcp_find_peer_fd(fd_first, fd_max, &(pkt.addr));
					if (fd < 0) continue;
					syslog(LOG_INFO, "< TIME %d (%d)\n", rx->seq, fd);
					if (send(fd, (char*)&tx, DATALEN, 0) == -1) {
						if (addr2str(&(pkt.addr), addrstr) == 0)
							syslog(LOG_ERR, "Client %s disconnected", addrstr);
						if (close(fd) < 0)
							syslog(LOG_ERR, "close: %s", strerror(errno));
					}
				} 
				if (pkt.data[0] == TYPE_PONG) {
					syslog(LOG_INFO, "> PONG %d\n", rx->seq);
				} 
				if (pkt.data[0] == TYPE_TIME) {
					syslog(LOG_INFO, "> TS %d\n", rx->seq);
				} 
			} else if (unix_fd_isset(s_tcp, &fs_tmp) == 1) {
				printf("tcp\n");
				/* TCP socket; Accept timestamp connection */
				slen = (socklen_t)sizeof (struct sockaddr_in6);
				memset(&addr_tmp, 0, sizeof addr_tmp);
				fd = accept(s_tcp, (struct sockaddr *)&addr_tmp, &slen);
				if (fd < 0) {
					syslog(LOG_ERR, "accept: %s", strerror(errno));
				} else {
					/* We don't expect to receive any data, just keep track */
					if (fd > fd_max)
						fd_max = fd;
					if (addr2str(&addr_tmp, addrstr) == 0)
						syslog(LOG_INFO, "Client %d %s connected", fd, addrstr);
					else 
						(void)close(fd);
				}
			} else if (unix_fd_isset(fd_pipe[0], &fs_tmp) == 1) {
				/* PIPE; timestamp from TCP client */
				char buf[DATALEN];
				if (read(fd_pipe[0], buf, DATALEN) < 0)
					syslog(LOG_ERR, "pipe: read: %s", strerror(errno));
				printf("PIPE HAHA %s\n", buf);
			}
		} else {
			/* Send PING */
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

int tcp_find_peer_fd(int fd_first, int fd_max, addr_t *peer) {
	int i;
	addr_t tmp;
	socklen_t slen;

	slen = (socklen_t)sizeof tmp;
	for (i = (fd_first + 1); i <= fd_max; i++) {
		if (getpeername(i, (struct sockaddr*)&tmp, &slen) == 0) {
			if (memcmp(&(peer->sin6_addr), &(tmp.sin6_addr), sizeof
						tmp.sin6_addr) == 0) {
				return i;
			}
		} else {
			if (errno != EBADF) {
				syslog(LOG_ERR, "Client %d disconnected", i);
				if (close(i) < 0)
					syslog(LOG_ERR, "close: %s", strerror(errno));
			}
		}
	}
	return -1;
}

int addr2str(addr_t *a, /*@out@*/ char *s) {
	if (inet_ntop(AF_INET6, &(a->sin6_addr), s, INET6_ADDRSTRLEN) == NULL) {
		syslog(LOG_ERR, "inet_ntop: %s", strerror(errno));
		return -1;
	}
	return 0;
}
/*
 * The client connects over TCP to the server, in order to
 * get reliable timestamps. The reason for forking is: being
 * able to use simple blocking connect() and read(), handling
 * state and timeouts in one context only, avoid conflicts with
 * the 'server' (parent) file descriptor set, for example when
 * doing bi-directional tests (both connecting to each other).
 */
int fork_client(int pipe, char *server, char *port) {
	int sock, r;
	char buf[DATALEN];
	struct addrinfo hints, *ai;
	addr_t *their;
	char addrstr[INET6_ADDRSTRLEN];
	struct packet_data *rx;
	fd_set fs;
	struct timeval tv;

printf("start client\n");
	if (signal(SIGCHLD, SIG_IGN) == SIG_ERR)
		syslog(LOG_ERR, "signal: SIG_IGN on SIGCHLD failied");
	if (fork() > 0) return 0;
	while (1 == 1) {
		memset(&hints, 0, sizeof hints);
		hints.ai_family = AF_INET6;
		hints.ai_flags = AI_V4MAPPED;
		hints.ai_socktype = SOCK_STREAM;
		r = getaddrinfo(server, port, &hints, &ai);
		if (r < 0) {
			syslog(LOG_ERR, "getaddrinfo: %s", gai_strerror(r));
			sleep(10);
			continue;
		}
		their = (struct sockaddr_in6*)ai->ai_addr;
		if (addr2str(their, addrstr) < 0) {
			sleep(10);
			continue;
		}
		syslog(LOG_INFO, "Connecting to server %s:%s\n", addrstr, port);
		sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);	
		if (sock < 0) {
			syslog(LOG_ERR, "socket: %s", strerror(errno));
			sleep(10);
			continue;
		}
		if (connect(sock, ai->ai_addr, ai->ai_addrlen) < 0) {
			syslog(LOG_ERR, "connect: %s", strerror(errno));
			(void)close(sock);
			sleep(10);
			continue;
		}
		while (1 == 1) {
			unix_fd_zero(&fs);
			unix_fd_set(sock, &fs);
			tv.tv_sec = 60;
			tv.tv_usec = 0;
			if (select(sock + 1, &fs, NULL, NULL, &tv) < 0) {
				syslog(LOG_ERR, "select: %s", strerror(errno));
				break;
			} 
			if (unix_fd_isset(sock, &fs) == 0) break;
			r = recv(sock, buf, DATALEN, 0);
			if (r == 0) break;
			if (r < 0) {
				syslog(LOG_ERR, "recv: %s", strerror(errno));
				break;
			}
			rx = (struct packet_data *)buf;
			write(pipe, "hej GOT IT", 48);
		}
		syslog(LOG_ERR, "Connection to server %s lost", addrstr);
		freeaddrinfo(ai);
		close(sock);
		sleep(1);
	}
	exit(EXIT_FAILURE);
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

