/*
 * UDP PING
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

/*
 * The main loop, this is where the magic happens. One UDP (ping/pong)
 * and many TCP sockets (timestamp) is used, and those were created by
 * the function 'bind_or_die'. SLA-NG probed can operate in client and
 * server mode simultaneously. Client mode probes, sending PINGs, have
 * one forked TCP process each, connecting to the server, in order to
 * receive timestamps reliably. The server mode responder accepts TCP
 * connections, but doesn't fork. It simply keeps the TCP file
 * descriptor as long as the client is alive, sending timestamp packets
 * over it.
 *
 * CLIENT MODE
 *  loop: wait for time to send > send UDP ping > save tstamp
 *  loop: wait for UDP pong > save tstamp
 *  loop: wait for pipe tstamp > save tstamp
 *  fork: connect > wait for TCP tstamp > write to pipe > wait...
 * 
 * SERVER MODE
 *  loop: wait for UDP ping > send UDP pong > find fd > send TCP tstamp
 *  loop: wait for TCP connect > add to fd set > remove dead fds 
 */
void loop_or_die(int s_udp, int s_tcp, /*@null@*/ char *addr, char *port) {
	struct addrinfo /*@dependent@*/ hints;
	struct addrinfo *ai;
	char addrstr[INET6_ADDRSTRLEN];
	struct sockaddr_in6 *their, addr_tmp, addr_last;
	pkt_t pkt;
	data_t *rx, tx, tx_last;
	struct timespec ts;
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
	/* PING results for clients */
	client_res_init(); 
	/* Address lookup */
	if (addr == NULL) {
		syslog(LOG_INFO, "Server mode: listening at %s\n", port);
		their = NULL;
		ai = NULL;
	} else {
		(void)client_fork(fd_pipe[1], addr, port);
		(void)sleep(1); // connect, wait! 
		(void)signal(SIGINT, client_res_summary);
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
	if (s_udp > fd_max) fd_max = s_udp;
	if (s_tcp > fd_max) fd_max = s_tcp;
	if (fd_pipe[0] > fd_max) fd_max = fd_pipe[0];
	if (fd_pipe[1] > fd_max) fd_max = fd_pipe[1];
	fd_first = fd_max;
	memset(&addr_last, 0, sizeof addr_last);
	memset(&tx_last, 0, sizeof tx_last);
	/* Let's loop those sockets! */
	while (1 == 1) {
		fs_tmp = fs;
		tv.tv_sec = 0;
		tv.tv_usec = 10000;
		if (select(fd_max + 1, &fs_tmp, NULL, NULL, &tv) > 0) {
			if (unix_fd_isset(s_udp, &fs_tmp) == 1) {
				/* UDP socket; PING and PONG */
				pkt.data[0] = '\0';
				if (recv_w_ts(s_udp, 0, &pkt) < 0)
					continue;
				rx = (data_t *)&pkt.data;
				if (pkt.data[0] == TYPE_PING) {
					syslog(LOG_DEBUG, "> PING %d\n", rx->seq);
					/* Send UDP PONG */
					tx.type = TYPE_PONG;
					tx.id = rx->id;
					tx.seq = rx->seq;
					tx.t2 = pkt.ts;
					(void)send_w_ts(s_udp, &(pkt.addr), (char*)&tx, &ts);
					/* Send TCP timestamp */
					tx.t3 = ts;
					tx.type = TYPE_TIME;
					/* In case of Intel RX timestamp error, kill last ts */
					if (pkt.ts.tv_sec == 0 && pkt.ts.tv_nsec == 0) {
						fd = server_find_peer_fd(fd_first, fd_max, &addr_last);
						syslog(LOG_ERR, "RX tstamp error, killing %d on %d",
								tx_last.seq, tx_last.id);
						if (fd >= 0) {
							tx_last.t2.tv_sec = 0;
							tx_last.t2.tv_nsec = 0;
							tx_last.t3.tv_sec = 0;
							tx_last.t3.tv_nsec = 0;
							(void)send(fd, (char*)&tx_last, DATALEN, 0);
						} 
					}
					/* Save addr and data for later, in case next is error */ 
					memcpy(&addr_last, &pkt.addr, sizeof addr_last);
					//tx_last = tx;
					/* Really send TCP */
					syslog(LOG_DEBUG, "< PONG %d\n", tx.seq);
					fd = server_find_peer_fd(fd_first, fd_max, &(pkt.addr));
					if (fd < 0) continue;
					syslog(LOG_DEBUG, "< TIME %d (%d)\n", rx->seq, fd);
					if (send(fd, (char*)&tx, DATALEN, 0) == -1) {
						if (addr2str(&(pkt.addr), addrstr) == 0)
							syslog(LOG_ERR, "server: %s disconnected", addrstr);
						if (close(fd) < 0)
							syslog(LOG_ERR, "close: %s", strerror(errno));
					}
				} 
				if (pkt.data[0] == TYPE_PONG) {
					syslog(LOG_DEBUG, "> PONG %d\n", rx->seq);
					client_res_update(&pkt.addr.sin6_addr, rx, &pkt.ts);
				} 
			} else if (unix_fd_isset(s_tcp, &fs_tmp) == 1) {
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
				if (read(fd_pipe[0], &pkt, sizeof pkt) < 0)
					syslog(LOG_ERR, "pipe: read: %s", strerror(errno));
				/* Security feature; make sure type is tstamp; ts is NULL */
				rx = (data_t *)&pkt.data;
				rx->type = 't';
				syslog(LOG_DEBUG, "> TS   %d\n", rx->seq);
				client_res_update(&pkt.addr.sin6_addr, rx, NULL);
			}
		} else {
			/* Send PING */
			if (their == NULL) continue;
			(void)gettimeofday(&now, 0);
			diff_tv(&tv, &now, &last);
			if (cmp_tv(&tv, &interval) == 1) {
				memset(&tx, 0, sizeof tx);
				tx.type = TYPE_PING;
				tx.id = 1;
				tx.seq = seq;
				if (send_w_ts(s_udp, their, (char*)&tx, &ts) < 0)
					continue;
				client_res_insert(&their->sin6_addr, &tx, &ts);
				(void)gettimeofday(&last, 0);
				syslog(LOG_DEBUG, "< PING %d\n", (int)seq);
				seq++;
			}
		}
	}
	if (ai != NULL) freeaddrinfo(ai);
}

/*
 * The function mapping an address 'peer' to a file descriptor.
 * It also removes dead peers, as that functionality comes for free
 * when doing 'getpeername'. Therefore, it needs to know the lowest
 * static (listening) fd, in order not to kill them as well.
 */
int server_find_peer_fd(int fd_first, int fd_max, addr_t *peer) {
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
				syslog(LOG_ERR, "server: %d disconnected", i);
				if (close(i) < 0)
					syslog(LOG_ERR, "server: close: %s", strerror(errno));
			}
		}
	}
	return -1;
}

/*
 * The client connects over TCP to the server, in order to
 * get reliable timestamps. The reason for forking is: being
 * able to use simple blocking connect() and read(), handling
 * state and timeouts in one context only, avoid conflicts with
 * the 'server' (parent) file descriptor set, for example when
 * doing bi-directional tests (both connecting to each other).
 */
int client_fork(int pipe, char *server, char *port) {
	int sock, r;
	struct addrinfo hints, *ai;
	char addrstr[INET6_ADDRSTRLEN];
	pkt_t pkt;
	fd_set fs;
	struct timeval tv;
	char log[100];
	ts_t zero;
	socklen_t slen;

	/* Create client fork */
	(void)snprintf(log, 100, "client: %s:", server);
	if (signal(SIGCHLD, SIG_IGN) == SIG_ERR)
		syslog(LOG_ERR, "%s signal: SIG_IGN on SIGCHLD failed", log);
	if (fork() > 0) return 0;
	/* We're going to send a struct packet over the pipe */
	memset(&pkt.ts, 0, sizeof zero);
	/* Try to stay connected to server; forever */
	while (1 == 1) {
		memset(&hints, 0, sizeof hints);
		hints.ai_family = AF_INET6;
		hints.ai_flags = AI_V4MAPPED;
		hints.ai_socktype = SOCK_STREAM;
		r = getaddrinfo(server, port, &hints, &ai);
		if (r < 0) {
			syslog(LOG_ERR, "%s getaddrinfo: %s", log, gai_strerror(r));
			(void)sleep(10);
			continue;
		}
		memcpy(&pkt.addr, ai->ai_addr, sizeof pkt.addr);
		freeaddrinfo(ai);
		if (addr2str(&pkt.addr, addrstr) < 0) {
			(void)sleep(10);
			continue;
		}
		syslog(LOG_INFO, "%s Connecting to %s port %s\n", log, addrstr, port);
		sock = socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);	
		if (sock < 0) {
			syslog(LOG_ERR, "%s socket: %s", log, strerror(errno));
			(void)sleep(10);
			continue;
		}
		slen = (socklen_t)sizeof pkt.addr;
		if (connect(sock, (struct sockaddr *)&pkt.addr, slen) < 0) {
			syslog(LOG_ERR, "%s connect: %s", log, strerror(errno));
			(void)close(sock);
			(void)sleep(10);
			continue;
		}
		while (1 == 1) {
			/* We use a 1 minute read timeout, otherwise reconnect */
			unix_fd_zero(&fs);
			unix_fd_set(sock, &fs);
			tv.tv_sec = 60;
			tv.tv_usec = 0;
			if (select(sock + 1, &fs, NULL, NULL, &tv) < 0) {
				syslog(LOG_ERR, "%s select: %s", log, strerror(errno));
				break;
			} 
			if (unix_fd_isset(sock, &fs) == 0) break;
			r = (int)recv(sock, &pkt.data, DATALEN, 0);
			if (r == 0) break;
			if (r < 0) {
				syslog(LOG_ERR, "%s recv: %s", log, strerror(errno));
				break;
			}
			if (write(pipe, (char *)&pkt, sizeof pkt) < 0) 
				syslog(LOG_ERR, "%s write: %s", log, strerror(errno));
		}
		syslog(LOG_ERR, "%s Connection lost", log);
		(void)close(sock);
		(void)sleep(1);
	}
	exit(EXIT_FAILURE);
}
/*
void client_print(c_res_t c_res, addr_t *addr, data_t *pkt, int t, ts_t *ts) {
	int i, res_num;

	for (i = 0; i < NUM_CLIENT_RES; i++) {
		if (c_res[i]->addr != addr) continue;
		if (c_res[i]->id != pkt->id) continue;
	   	if (c_res[i]->seq != pkt->seq) continue;
		res_num = i;
	}

}*/

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

/*void proto_timestamp() {
	pkt_t *pt;
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

