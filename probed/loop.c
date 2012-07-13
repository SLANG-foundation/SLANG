/*
 * Copyright (c) 2011 Anders Berggren, Lukas Garberg, Tele2
 *
 * We have not yet decided upon a license, and so far it may only be
 * used and redistributed with our explicit permission.
 */

/**
 * \file   loop.c
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
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <sys/time.h>
#include <sys/queue.h>
#include "probed.h"
#include "loop.h"
#include "unix.h"
#include "util.h"
#include "net.h"
#include "client.h"

struct server_peer {
	addr_t addr;
	int fd;
	LIST_ENTRY(server_peer) list;
};
static LIST_HEAD(peers_listhead, server_peer) peers_head;

static int server_find_peer_fd(addr_t *addr);
static void server_kill_peer(fd_set *fs, int *fd_max, int fd);

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
 * \param[in] s_udp   Listening UDP socket to use for PING/PONG
 * \param[in] s_tcp   Listening TCP socket for client accept and TSTAMP
 * \param[in] port    client_msess_reconf's getaddrinfo needs the port
 * \param[in] cfgpath client_msess_reconf needs XML config
 * \bug       The 'first', not 'correct' TCP client socket will be used
 */
void loop_or_die(int s_udp, int s_tcp, char *port, char *cfgpath) {
	struct server_peer *p;
	char addrstr[INET6_ADDRSTRLEN];
	char byte;
	addr_t addr_tmp;
	pkt_t pkt;
	data_t *rx, tx;
	ts_t ts, last_stats, now, tmp_ts;
	fd_set fs_tmp;
	int i, fd, fd_client_low, sends = 0, fd_max = 0;
	fd_set fs;
	socklen_t slen;
	int fd_client_pipe[2];
	int fd_send_pipe[2];
	int ok = 1;

	LIST_INIT(&peers_head);
	/* IPC for children-to-parent (TCP client to UDP state machine) */
	if (pipe(fd_client_pipe) < 0) {
		syslog(LOG_ERR, "pipe: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (pipe(fd_send_pipe) < 0) {
		syslog(LOG_ERR, "pipe: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (fcntl(fd_send_pipe[1], F_SETFL, O_NONBLOCK) < 0) {
		syslog(LOG_ERR, "fcntl: %s; fd_send_pipe[1]", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (fcntl(fd_send_pipe[0], F_SETFL, O_NONBLOCK) < 0) {
		syslog(LOG_ERR, "fcntl: %s; fd_send_pipe[0]", strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* set last stats timer */
	(void)clock_gettime(CLOCK_REALTIME, &last_stats);


	/* Transmit timer */
	client_send_fork(fd_send_pipe[1]);

	/* Add both pipe, UDP and TCP to the FD set, note highest FD */
	unix_fd_zero(&fs);
	unix_fd_zero(&fs_tmp);
	unix_fd_set(s_udp, &fs);
	unix_fd_set(s_tcp, &fs);
	unix_fd_set(s_tcp, &fs);
	unix_fd_set(fd_client_pipe[0], &fs);
	unix_fd_set(fd_send_pipe[0], &fs);
	fd_max = MAX(fd_max, s_udp);
	fd_max = MAX(fd_max, s_tcp);
	fd_max = MAX(fd_max, fd_client_pipe[0]);
	fd_max = MAX(fd_max, fd_client_pipe[1]);
	fd_max = MAX(fd_max, fd_send_pipe[0]);
	fd_max = MAX(fd_max, fd_send_pipe[1]);
	fd_client_low = fd_max;

	/* Let's loop those sockets! */
	while (1 == 1) {
		fs_tmp = fs;
		if (select(fd_max + 1, &fs_tmp, NULL, NULL, NULL) > 0) {
			ok = 0;
			/* CLIENT/SERVER: UDP socket, that is PING and PONG */
			if (unix_fd_isset(s_udp, &fs_tmp) == 1) {
				ok = 1;
				if (recv_w_ts(s_udp, 0, &pkt) < 0)
					ok = 0;
				rx = (data_t *)&pkt.data;
				/* SERVER: Send UDP PONG */
				if (ok == 1 && rx->type == TYPE_PING) {
					count_server_resp++;
					tx.type = TYPE_PONG;
					tx.id = rx->id;
					tx.seq = rx->seq;
					last_tx_id = rx->id;
					last_tx_seq = rx->seq;
					tx.t2 = pkt.ts;
					(void)dscp_set(s_udp, pkt.dscp);
					(void)send_w_ts(s_udp, &(pkt.addr), (char*)&tx, &ts);
					/* Send TCP timestamp */
					tx.type = TYPE_TIME;
					tx.t3 = ts;
					fd = server_find_peer_fd(&pkt.addr);
					if (fd < 0) continue;
					if (send(fd, (char*)&tx, DATALEN, 0) != DATALEN) {
						server_kill_peer(&fs, &fd_max, fd);
					}
				}
				/* CLIENT: Update results with received UDP PONG */
				if (ok == 1 && rx->type == TYPE_PONG) {
					client_res_update(&pkt.addr, rx, &pkt.ts, pkt.dscp);
				}
			}
			/* SERVER: TCP socket, accept timestamp connection */
			if (unix_fd_isset(s_tcp, &fs_tmp) == 1) {
				ok = 1;
				slen = (socklen_t)sizeof (addr_t);
				memset(&addr_tmp, 0, sizeof addr_tmp);
				fd = accept(s_tcp, (struct sockaddr *)&addr_tmp, &slen);
				if (fd < 0) {
					syslog(LOG_ERR, "accept: %s", strerror(errno));
					ok = 0;
				}
				if (ok == 1 && addr2str(&addr_tmp, addrstr) == 0)
					syslog(LOG_INFO, "server: %s: %d: Connected", addrstr, fd);
				else
					ok = 0;
				if (ok == 1) {
					/* Keep track of client's FD */
					unix_fd_set(fd, &fs);
					if (fd > fd_max)
						fd_max = fd;
					/* Send hello, feed me with PINGs */
					memset(&tx, 0, sizeof tx);
					tx.type = TYPE_HELO;
					if (send(fd, (char*)&tx, DATALEN, 0) != DATALEN) {
						server_kill_peer(&fs, &fd_max, fd);
					}
					p = malloc(sizeof *p);
					if (p == NULL) return;
					p->fd = fd;
					memcpy(&p->addr, &addr_tmp, sizeof p->addr);
					LIST_INSERT_HEAD(&peers_head, p, list);
				}
			}
			/* CLIENT: PIPE; timestamps from client_fork (TCP) */
			if (unix_fd_isset(fd_client_pipe[0], &fs_tmp) == 1) {
				ok = 1;
				if (read(fd_client_pipe[0], &pkt, sizeof pkt) < 0) {
					syslog(LOG_ERR, "pipe: read: %s", strerror(errno));
					ok = 0;
				}
				rx = (data_t *)&pkt.data;
				if (ok == 1 && rx->type == TYPE_HELO) {
					/* Connected to server, ready to feed it! */
					if (client_msess_gothello(&pkt.addr) != 0)
						syslog(LOG_INFO, "client: Unknown client connected");
					if (addr2str(&pkt.addr, addrstr) == 0)
						syslog(LOG_INFO, "client: %s: Connected", addrstr);
				} else if (ok == 1 && rx->type == TYPE_TIME) {
					rx = (data_t *)&pkt.data;
					client_res_update(&pkt.addr, rx, NULL, -1);
				}
			}
			/* CLIENT: PIPE; send */
			if (unix_fd_isset(fd_send_pipe[0], &fs_tmp) == 1) {

				ok = 0;

				/* Read pipe as long as we have data.
				 * If the pipe hasn't been read for a while, we might have a
				 * large number of messages here, but should only trigger
				 * transmit function once...*/
				while (read(fd_send_pipe[0], &byte, sizeof byte) > 0) {
					ok++;
				}

				/* Did we receive any message? */
				if (ok > 0) {

					/* Warn if we had more than one message queued */
					if (ok > 1) {
						syslog(LOG_ERR, "Found %d trigger messages queued",
							ok);
					}

					/* trigger client packet transmission*/
					client_msess_transmit(s_udp, sends);

					/* reload if requested */
					if (cfg.should_reload == 1) {
						cfg.should_reload = 0;
						(void)client_msess_reconf(port, cfgpath);
						client_msess_forkall(fd_client_pipe[1]);
					}

					/* clear timed out probes every now and then */
					if (sends % (TIMEOUT_INTERVAL/SEND_INTERVAL) == 0) {
						client_res_clear_timeouts();
					}

					/* log statistics */
					if (sends % 10000 == 0 &&
							cfg.op == DAEMON) {

						/* calculate time since last statistics report */
						(void)clock_gettime(CLOCK_REALTIME, &now);
						diff_ts(&tmp_ts, &now, &last_stats);
						memcpy(&last_stats, &now, sizeof last_stats);
						syslog(LOG_INFO, "stats_delay:        %d.%d",
								(int)tmp_ts.tv_sec, (int)tmp_ts.tv_nsec);

						syslog(LOG_INFO, "count_server_resp:  %d (pps*10)",
								count_server_resp);
						syslog(LOG_INFO, "count_client_sent:  %d (pps*10)",
								count_client_sent);
						syslog(LOG_INFO, "count_client_done:  %d (pps*10)",
								count_client_done);
						syslog(LOG_INFO, "count_client_find:  %d (1)",
								count_client_find);
						syslog(LOG_INFO, "count_client_fifoq: %d (0)",
								count_client_fifoq);
						syslog(LOG_INFO, "count_client_fqmax: %d (0)",
								count_client_fifoq_max);
						count_server_resp = 0;
						count_client_sent = 0;
						count_client_done = 0;
					}

					sends++;

				} else {
					syslog(LOG_ERR, "pipe: read: %s", strerror(errno));
				}

			}
			/* It's a client. They shouldn't speak, it's probably a
			 * disconnect. KILL IT. */
			if (ok == 0) {
				for (i = fd_client_low; i <= fd_max; i++) {
					if (unix_fd_isset(i, &fs_tmp) == 1) {
						server_kill_peer(&fs, &fd_max, i);
					}
				}
			}
		} else {
			/* select() timeout */
			syslog(LOG_ERR, "select: %s", strerror(errno));
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
 * \param[in] fd_max   The highest client file descriptor
 * \param[in] peer     Pointer to IP address to find socket for
 * \return             File descriptor to client socket of address 'peer'
 */
static int server_find_peer_fd(addr_t *addr) {
	struct server_peer *p;
	size_t len;
	
	len = sizeof addr->sin6_addr;
	for (p = peers_head.lh_first; p != NULL; p = p->list.le_next)
		if (memcmp(&p->addr.sin6_addr, &addr->sin6_addr, len) == 0)
			return p->fd;
	return -1;
}

/**
 * The function killing an client peer socket file descriptor, modifies
 * global variables fs; the file descriptor set, and fd_max.
 *
 * \param[out] fs       Pointer to file descriptor set
 * \param[out] fd_ax    Pointer  to the highest client file descriptor
 * \param[in]  fd       Client file descriptor
 */
static void server_kill_peer(fd_set *fs, int *fd_max, int fd) {
	char addrstr[INET6_ADDRSTRLEN];
	struct server_peer *p;
	addr_t tmp;
	socklen_t slen;

	/* Print disconnect message */
	memset(&tmp, 0, sizeof tmp);
	slen = (socklen_t)sizeof tmp;
	if (getpeername(fd, (struct sockaddr*)&tmp, &slen) == 0) {
		if (addr2str(&tmp, addrstr) == 0) {
			syslog(LOG_INFO, "server: %s: %d: Disconnected", addrstr, fd);
		} else syslog(LOG_INFO, "server: %d: Disconnected", fd);
	} else syslog(LOG_INFO, "server: %d: Disconnected", fd);
	/* Remove linked list */
	for (p = peers_head.lh_first; p != NULL; p = p->list.le_next) {
		if (p->fd == fd) {
			LIST_REMOVE(p, list);
			free(p);
			break; /* Otherwise for() crashes! */
		}
	}
	/* Close, clear fd set, maybe decrease fd_max */
	if (close(fd) < 0)
		syslog(LOG_ERR, "server: close: %s", strerror(errno));
	unix_fd_clr(fd, fs);
	if (fd == *fd_max)
		(*fd_max)--;
}
