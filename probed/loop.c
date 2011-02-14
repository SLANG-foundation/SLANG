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
#include "unix.h"
#include "util.h"
#include "net.h"
#include "client.h"

static int server_find_peer_fd(int fd_first, addr_t *peer);
static void server_kill_peer(int fd);
static fd_set fs;
static int fd_max = 0;

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

	char addrstr[INET6_ADDRSTRLEN];
	struct sockaddr_in6 addr_tmp, addr_last;
	pkt_t pkt;
	data_t *rx, tx, tx_last;
	ts_t ts;
	struct timeval tv;
	fd_set fs_tmp;
	int fd, fd_first, i;
	int fd_pipe[2];
	socklen_t slen;

	/* IPC for children-to-parent (TCP client to UDP state machine) */
	if (pipe(fd_pipe) < 0) {
		syslog(LOG_ERR, "pipe: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

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
			/* CLIENT/SERVER: UDP socket, that is PING and PONG */
			if (unix_fd_isset(s_udp, &fs_tmp) == 1) {
				pkt.data[0] = '\0';
				if (recv_w_ts(s_udp, 0, &pkt) < 0)
					continue;
				rx = (data_t *)&pkt.data;
				/* SERVER: Send UDP PONG */
				if (pkt.data[0] == TYPE_PING) {
					tx.type = TYPE_PONG;
					tx.id = rx->id;
					tx.seq = rx->seq;
					tx.t2 = pkt.ts;
					(void)dscp_set(s_udp, pkt.dscp);
					(void)send_w_ts(s_udp, &(pkt.addr), (char*)&tx, &ts);
					/* Send TCP timestamp */
					tx.t3 = ts;
					tx.type = TYPE_TIME;
					fd = server_find_peer_fd(fd_first, &(pkt.addr));
					if (fd < 0) continue;
					if (send(fd, (char*)&tx, DATALEN, 0) != DATALEN) {
						server_kill_peer(fd);
					}
				} 
				/* CLIENT: Update results with received UDP PONG */
				if (pkt.data[0] == TYPE_PONG) {
					client_res_update(&pkt.addr.sin6_addr, rx, &pkt.ts);
				} 
			/* SERVER: TCP socket, accept timestamp connection */
			} else if (unix_fd_isset(s_tcp, &fs_tmp) == 1) {
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
				/* Keep track of client's FD, although it will be quiet */
				unix_fd_set(fd, &fs);
				if (fd > fd_max)
					fd_max = fd;
				/* Send hello, feed me with PINGs */
				memset(&tx, 0, sizeof tx);
				tx.type = TYPE_HELO;
				if (send(fd, (char*)&tx, DATALEN, 0) != DATALEN) {
					server_kill_peer(fd);
				}
			/* CLIENT: PIPE; timestamps from client_fork (TCP) */
			} else if (unix_fd_isset(fd_pipe[0], &fs_tmp) == 1) {
				if (read(fd_pipe[0], &pkt, sizeof pkt) < 0) {
					syslog(LOG_ERR, "pipe: read: %s", strerror(errno));
					continue;
				}
				rx = (data_t *)&pkt.data;
				if (rx->type == TYPE_HELO) {
					/* Connected to server, ready to feed it! */
					if (client_msess_gothello(&pkt.addr) != 0)
						syslog(LOG_INFO, "client: Unknown client connected");
					if (addr2str(&pkt.addr, addrstr) == 0)
						syslog(LOG_INFO, "client: %s: Connected", addrstr);
				} else { /* Implicit: type == TYPE_TS */
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
		/* CLIENT: select() timeout, time to send data and reload config */
		} else {
			/* Send PINGs to all clients */
			client_msess_transmit(s_udp);
			/* Configuration reload, shoudreload is set on SIGHUP */
			if (cfg.shouldreload == 1) {
				cfg.shouldreload = 0;
				(void)client_msess_reconf(port, cfgpath);
				client_msess_forkall(fd_pipe[0]);
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
	memset(&tmp, 0, sizeof tmp);
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
