/**
 * \file   mainloop.c
 * \brief  The main loop for SLA-NG, handling client/server operations
 * \author Anders Berggren <anders@halon.se>
 * \author Lukas Garberg <lukas@spritelink.net>
 * \date   2011-01-20
 * \bug    Only one 'probed' UDP timestmap socket can be used at a time
 */

#include <stdlib.h>
#ifndef S_SPLINT_S /* SPlint 3.1.2 bug */
#include <unistd.h>
#endif
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <sys/time.h>
#include "probed.h"
#include "loop.h"
#include "msess.h"
#include "unix.h"
#include "util.h"
#include "net.h"
#include "client.h"

static int server_find_peer_fd(int fd_first, addr_t *peer);
static void server_kill_peer(int fd);
fd_set fs;
int fd_max = 0;

/**
 * Main SLA-NG 'probed' state machine, handling all client/server stuff
 *
 * The main loop, this is where the magic happens. One UDP (ping/pong)
 * and many TCP sockets (timestamp) is used, and those were created by
 * the function bind_or_die(). SLA-NG probed can operate in client and
 * server mode simultaneously. Client mode probes, sending PINGs, have
 * one forked TCP process each, connecting to the server, in order to
 * receive timestamps reliably. The server mode responder accepts TCP
 * connections, but doesn't fork. It simply keeps the TCP file
 * descriptor as long as the client is alive, sending timestamp packets
 * over it. We use server_find_peer_fd() to map the address of incoming
 * UDP pongs to a TCP timestamp client socket.
 *
 * CLIENT MODE                                                     \n
 *  loop: wait for time to send > send ping > save tstamp          \n
 *  loop: wait for pong > save tstamp                              \n
 *  loop: wait for pipe tstamp > save tstamp                       \n
 *  fork: connect > wait for TCP tstamp > write to pipe > wait...  \n
 * 
 * SERVER MODE                                                     \n
 *  loop: wait for ping > send pong > find fd > send TCP tstamp    \n
 *  loop: wait for TCP connect > add to fd set > remove dead fds   \n
 *
 * \param[in] s_udp A listening UDP socket to use for PING/PONG
 * \param[in] s_tcp A listening TCP socket for client accept and TSTAMP
 * \bug       The 'first', not 'correct' TCP client socket will be used
 */
void loop_or_die(int s_udp, int s_tcp) {

	char addrstr[INET6_ADDRSTRLEN];
	struct sockaddr_in6 addr_tmp, addr_last;
	pkt_t pkt;
	data_t *rx, tx, tx_last;
	struct timespec ts;
	struct timeval tv, last, now;
	fd_set fs_tmp;
	int fd, fd_first, i;
	int fd_pipe[2];
	socklen_t slen;

	struct msess *sess, *findsess;

	/* IPC for children-to-parent (TCP client to UDP state machine) */
	if (pipe(fd_pipe) < 0) {
		syslog(LOG_ERR, "pipe: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}
	/* For CLIENT and DAEMON, start 'clients'  */
	if (cfg.op == CLIENT || cfg.op == DAEMON) {
		/* PING results, both CLIENT and DAEMON */
		client_res_init(); 
		/* Spawn client forks for all measurement sessions */
		while ((sess = msess_next()) != NULL) {
			/* Make sure there is no fork already running with 
			 * the same destination address */
			if ((findsess = msess_find_running_addr(&sess->dst)) != NULL) {
				sess->child_pid = findsess->child_pid;
				continue;
			}
			sess->child_pid = client_fork(fd_pipe[1], &sess->dst);
		}
	}

	/* Timers for sending data */
	(void)gettimeofday(&last, 0);
	/* Add both pipe, UDP and TCP to the FD set, note highest FD */
	unix_fd_zero(&fs);
	unix_fd_zero(&fs_tmp);
	unix_fd_set(s_udp, &fs);
	unix_fd_set(s_tcp, &fs);
	unix_fd_set(s_tcp, &fs);
	unix_fd_set(fd_pipe[0], &fs);
	fd_max = MAX(fd_max, s_udp);
	fd_max = MAX(fd_max, s_tcp);
	fd_max = MAX(fd_max, fd_pipe[0]);
	fd_max = MAX(fd_max, fd_pipe[1]);
	fd_first = fd_max;
	memset(&addr_last, 0, sizeof addr_last);
	memset(&tx_last, 0, sizeof tx_last);
	/* Let's loop those sockets! */
	while (1 == 1) {
		fs_tmp = fs;
		tv.tv_sec = 0;
		tv.tv_usec = 100;
		if (select(fd_max + 1, &fs_tmp, NULL, NULL, &tv) > 0) {
			if (unix_fd_isset(s_udp, &fs_tmp) == 1) {
				/* UDP socket; PING and PONG */
				pkt.data[0] = '\0';
				if (recv_w_ts(s_udp, 0, &pkt) < 0)
					continue;
				rx = (data_t *)&pkt.data;
				if (pkt.data[0] == TYPE_PING) {
					/* Send UDP PONG */
					tx.type = TYPE_PONG;
					tx.id = rx->id;
					tx.seq = rx->seq;
					tx.t2 = pkt.ts;
					(void)dscp_set(s_udp, pkt.dscp);
					(void)send_w_ts(s_udp, &(pkt.addr), (char*)&tx, &ts);
					/* Send TCP timestamp */
					tx.t3 = ts;
					tx.type = TYPE_TIME;
					/* In case of Intel RX timestamp error, kill last ts */
					if (pkt.ts.tv_sec == 0 && pkt.ts.tv_nsec == 0) {
						fd = server_find_peer_fd(fd_first, &addr_last);
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
					tx_last = tx;
					/* Really send TCP */
					fd = server_find_peer_fd(fd_first, &(pkt.addr));
					if (fd < 0) continue;
					if (send(fd, (char*)&tx, DATALEN, 0) != DATALEN) {
						server_kill_peer(fd);
					}
				} 
				if (pkt.data[0] == TYPE_PONG) {
					client_res_update(&pkt.addr.sin6_addr, rx, &pkt.ts);
				} 
			} else if (unix_fd_isset(s_tcp, &fs_tmp) == 1) {
				/* TCP socket; Accept timestamp connection */
				slen = (socklen_t)sizeof (struct sockaddr_in6);
				memset(&addr_tmp, 0, sizeof addr_tmp);
				fd = accept(s_tcp, (struct sockaddr *)&addr_tmp, &slen);
				if (fd < 0) {
					syslog(LOG_ERR, "accept: %s", strerror(errno));
					continue;
				}
				if (addr2str(&addr_tmp, addrstr) == 0)
					syslog(LOG_INFO, "server: %s: %d: Connected", addrstr, fd);
				else
					continue;
				/* We don't expect to receive any data, just keep track */
				unix_fd_set(fd, &fs);
				if (fd > fd_max)
					fd_max = fd;
				/* Hello, feed me with PINGs */
				memset(&tx, 0, sizeof tx);
				tx.type = TYPE_HELO;
				if (send(fd, (char*)&tx, DATALEN, 0) != DATALEN) {
					server_kill_peer(i);
				}
			} else if (unix_fd_isset(fd_pipe[0], &fs_tmp) == 1) {
				/* PIPE; timestamp from TCP client */
				if (read(fd_pipe[0], &pkt, sizeof pkt) < 0) {
					syslog(LOG_ERR, "pipe: read: %s", strerror(errno));
					continue;
				}
				rx = (data_t *)&pkt.data;
				/* Connected to server, ready to feed it! */
				if (rx->type == TYPE_HELO) {
					if (addr2str(&pkt.addr, addrstr) == 0)
						syslog(LOG_INFO, "client: %s: Connected", addrstr);
					while ((sess = msess_next()) != NULL) {
						if (memcmp(&pkt.addr.sin6_addr, &sess->dst.sin6_addr, 
									sizeof sess->dst.sin6_addr) == 0) {
							sess->got_hello = 1;
						}
					}
				} else {
					/* Security; make sure type is tstamp; ts is NULL */
					rx->type = 't';
					client_res_update(&pkt.addr.sin6_addr, rx, NULL);
				}
			} else {
				/* It's a client. They shouldn't speak, it's probably a
				 * disconnect. KILL IT. */
				for (i = (fd_first + 1); i <= fd_max; i++) {
					if (unix_fd_isset(i, &fs_tmp) == 1) {
						server_kill_peer(i);
					}	
				}
			}
		} else {
			/* Send PING */
			(void)gettimeofday(&now, 0);
			while ((sess = msess_next()) != NULL) {
				/* Are we connected to server? */
				if (sess->got_hello != 1) 
					continue;
				diff_tv(&tv, &now, &sess->last_sent);
				/* time to send new packet? */
				if (cmp_tv(&tv, &sess->interval) == 1) {
					memset(&tx, 0, sizeof tx);
					tx.type = TYPE_PING;
					tx.id = sess->id;
					tx.seq = msess_get_seq(sess);
					(void)dscp_set(s_udp, sess->dscp);
					if (send_w_ts(s_udp, &sess->dst, (char*)&tx, &ts) < 0)
						continue;
					client_res_insert(&sess->dst.sin6_addr, &tx, &ts);
					memcpy(&sess->last_sent, &now, sizeof now);

				}
			}
		}
	}
}

/**
 * The function mapping an address 'peer' to a socket file descriptor
 * 
 * It also removes dead peers, as that functionality comes for free
 * when doing 'getpeername'. Therefore, it needs to know the lowest
 * static (listening) fd, in order not to kill them as well. It 
 * modifies the global variable fd_max; the heighest seen fd in fs.
 *
 * \param[in] fd_first The lowest dynamic (client) file descriptor 
 * \param[in] peer     Pointer to IP address to find socket for 
 * \return             File descriptor to client socket of address 'peer'
 */
static int server_find_peer_fd(int fd_first, addr_t *peer) {
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
				server_kill_peer(i);
			}
		}
	}
	return -1;
}

/**
 * The function killing an client peer socket file descriptor, modifies
 * global variables fs; the file descriptor set, and fd_max.
 * 
 * \param[in] fd       The lowest dynamic (client) file descriptor 
 */
static void server_kill_peer(int fd) {
	char addrstr[INET6_ADDRSTRLEN];
	addr_t tmp;
	socklen_t slen;

	/* Print disconnect message */
	slen = (socklen_t)sizeof tmp;
	if (getpeername(fd, (struct sockaddr*)&tmp, &slen) == 0) {
		if (addr2str(&tmp, addrstr) == 0) {
			syslog(LOG_INFO, "server: %s: %d: Disconnected", addrstr, fd);
		} else syslog(LOG_INFO, "server: %d: Disconnected", fd);
	} else syslog(LOG_INFO, "server: %d: Disconnected", fd);

	/* Close, clear fd set, maybe decrease fd_max */
	if (close(fd) < 0)
		syslog(LOG_ERR, "server: close: %s", strerror(errno));
	unix_fd_clr(fd, &fs);
	if (fd == fd_max)
		fd_max--;
}
