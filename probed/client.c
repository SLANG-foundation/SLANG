/**
 * \file   client.c
 * \brief  Contains 'client' (PING) specific code, and result handling  
 * \author Anders Berggren <anders@halon.se>
 * \author Lukas Garberg <lukas@spritelink.net>
 * \date   2011-01-10
 * \todo   Lots of LINT (splint) warnings that I don't understand
 */

#ifndef S_SPLINT_S /* SPlint 3.1.2 bug */
#include <unistd.h>
#endif
#include <string.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include "probed.h"

#define FIFO "/tmp/probed.fifo"

#define STATE_PING 'i'
#define STATE_GOT_TS 't' /* Because of Intel RX timestamp bug */
#define STATE_GOT_PONG 'o' /* Because of Intel RX timestamp bug */
#define STATE_READY 'r'
#define STATE_TSERROR 'e' /* Missing timestamp (Intel...?) */
#define STATE_TIMEOUT 't'

/*@ -exportlocal TODO wtf */
int res_response = 0;
int res_timeout = 0;
int res_pongloss = 0;
int res_tserror = 0;
long long res_rtt_total = 0;
ts_t res_rtt_min, res_rtt_max;
/*@ +exportlocal */

/**
 * Forks a 'client' process that connects to a server to get timestamps
 *
 * The client connects over TCP to the server, in order to
 * get reliable timestamps. The reason for forking is: being
 * able to use simple blocking connect() and read(), handling
 * state and timeouts in one context only, avoid conflicts with
 * the 'server' (parent) file descriptor set, for example when
 * doing bi-directional tests (both connecting to each other).
 * \param pipe   The main loop pipe file descriptor to send timestamps to
 * \param server The server address to connect to, and read timestamps from
 * \return       The process ID of the forked process
 * \bug          The read timeout of 60 sec before re-connect is bad!
 * \todo         Should we simply send a dummy packet, just for conn status?
 */

pid_t client_fork(int pipe, struct sockaddr_in6 *server) {
	int sock, r;
	pid_t client_pid;
	char addrstr[INET6_ADDRSTRLEN];
	pkt_t pkt;
	fd_set fs;
	struct timeval tv;
	char log[100];
	ts_t zero;
	socklen_t slen;

	/* Create client fork - parent returns */
	if (addr2str(server, addrstr) < 0)
		return -1;
	(void)snprintf(log, 100, "client: %s:", addrstr);
	if (signal(SIGCHLD, SIG_IGN) == SIG_ERR)
		syslog(LOG_ERR, "%s signal: SIG_IGN on SIGCHLD failed", log);
	client_pid = fork();
	if (client_pid > 0) return client_pid;

	/* 
	 * We are child 
	 */

	/* We're going to send a struct packet over the pipe */
	memset(&pkt.ts, 0, sizeof zero);

	/* Try to stay connected to server; forever */
	while (1 == 1) {
		memcpy(&pkt.addr, server, sizeof pkt.addr);
		if (addr2str(&pkt.addr, addrstr) < 0) {
			(void)sleep(10);
			continue;
		}
		syslog(LOG_INFO, "%s Connecting to %s port %d\n", log, 
				addrstr, ntohs(server->sin6_port));
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

/**
 * Initializes global variables and a FIFO used by client_res_* functions
 *
 * When running in deamon mode, open a FIFO. Always init global variables
 * such as the res_head linked list. Should be run once.
 * 
 * @todo Is the really good to wait for FIFO open?
 */
void client_res_init(void) {
	if (cfg.op == OPMODE_DAEMON) {
		(void)unlink(FIFO);
		if (mknod(FIFO, (__mode_t)S_IFIFO | 0644, (__dev_t)0) < 0) {
			syslog(LOG_ERR, "mknod: %s: %s", FIFO, strerror(errno));
			exit(EXIT_FAILURE);
		}
		syslog(LOG_INFO, "Waiting for listeners on FIFO %s", FIFO);
		cfg.fifo = open(FIFO, O_WRONLY);
	}
	/*@ -mustfreeonly -immediatetrans TODO wtf */
	LIST_INIT(&res_head);
	/*@ +mustfreeonly +immediatetrans */
	res_rtt_min.tv_sec = 99999;
	res_rtt_min.tv_nsec = 0;
	res_rtt_max.tv_sec = 0;
	res_rtt_max.tv_nsec = 0;
	/*@ -nullstate TODO wtf? */
	return;
	/*@ +nullstate */
}

/**
 * Insert a new 'ping' into the result list
 *
 * Should be run once for each 'ping', inserting timestamp T1.
 *
 * \param a  The server IP address that is being pinged
 * \param d  The ping data, such as sequence number, session ID, etc.
 * \param ts Pointer to the timestamp T1
 */
void client_res_insert(struct in6_addr *a, data_t *d, ts_t *ts) {
	struct res *r;

	r = malloc(sizeof *r);
	if (r == NULL) return;
	memset(r, 0, sizeof *r);
	(void)clock_gettime(CLOCK_REALTIME, &r->created);
	r->state = STATE_PING;
	memcpy(&r->addr, a, sizeof r->addr);
	r->id = d->id;
	r->seq = d->seq;
	r->ts[0] = *ts;
	/*@ -mustfreeonly -immediatetrans TODO wtf */
	LIST_INSERT_HEAD(&res_head, r, res_list);
	/*@ +mustfreeonly +immediatetrans */
	client_res_update(a, d, ts);
	/*@ -compmempass TODO wtf? */
	return;
	/*@ +compmempass */
}

/**
 * Update and print results when new timestamp data arrives
 *
 * Can be running multiple times, updating the timestamps of a ping. If
 * all timestamp info is present, also print the results or send them on
 * the daemon FIFO.
 *
 * \param a  The server IP address that is being pinged
 * \param d  The ping data, such as sequence number, timestamp T2 and T3.. 
 * \param ts Pointer to a timestamp, such as T4
 * \warning  We wait until the next timestamp arrives, before printing
 */
void client_res_update(struct in6_addr *a, data_t *d, /*@null@*/ ts_t *ts) {
	struct res *r, *r_tmp;
	ts_t now, diff, rtt;
	char old_state;
	int i;

	(void)clock_gettime(CLOCK_REALTIME, &now);
	r = res_head.lh_first;
	while (r != NULL) {
		/* If match; update */
		old_state = r->state;
		if (r->id == d->id &&
			r->seq == d->seq &&
			memcmp(&r->addr, a, sizeof r->addr) == 0) {
			if (d->type == 'o') {
				if (r->state == STATE_PING)
					r->state = STATE_GOT_PONG;
				if (r->state == STATE_GOT_TS)
					r->state = STATE_READY;
				if (ts != NULL) r->ts[3] = *ts;
				else syslog(LOG_ERR, "client_res: t4 missing");
			} else if (d->type == 't') {
				if (r->state == STATE_PING)
					r->state = STATE_GOT_TS;
				if (r->state == STATE_GOT_PONG)
					r->state = STATE_READY;
				r->ts[1] = d->t2;
				r->ts[2] = d->t3;
			}
		}
		/* Because of Intel RX timestamp bug, wait until next TS to print 
		 * in order to have time to correct a previous timestamp */
		if (old_state == STATE_READY && d->type == 't') {
			/* Check that all timestamps are present */
			for (i = 0; i < 4; i++) { 
				if (r->ts[i].tv_sec == 0 && r->ts[i].tv_nsec == 0) {
					syslog(LOG_ERR, "client_res: Ping %d from %d missing T%d",
							(int)r->seq, (int)r->id, i+1);
					r->state = STATE_TSERROR;
				}
			}
			/* Pipe (daemon) output */
			if (cfg.op == OPMODE_DAEMON) 
				if (write(cfg.fifo, (char*)r, sizeof *r) == -1)
					syslog(LOG_ERR, "daemon: write: %s", strerror(errno));
			/* Client output */
			if (cfg.op == OPMODE_CLIENT) { 
				if (r->state == STATE_TSERROR) {
					res_tserror++;
				} else {
					diff_ts(&diff, &r->ts[3], &r->ts[0]);
					diff_ts(&now, &r->ts[2], &r->ts[1]);
					diff_ts(&rtt, &diff, &now);
					if (rtt.tv_sec > 0)
						printf("Response %4d from %d in %10ld.%09ld\n", 
								(int)r->seq, (int)r->id, rtt.tv_sec, 
								rtt.tv_nsec);
					else 
						printf("Response %4d from %d in %ld ns\n", 
								(int)r->seq, (int)r->id, rtt.tv_nsec);
					res_response++;
					if (cmp_ts(&res_rtt_max, &rtt) == -1)
						res_rtt_max = rtt;	
					if (cmp_ts(&res_rtt_min, &rtt) == 1)
						res_rtt_min = rtt;
					res_rtt_total = res_rtt_total + rtt.tv_nsec;
				}
			}
			/* Ready; safe removal */
			r_tmp = r->res_list.le_next;
			/*@ -branchstate -onlytrans TODO wtf */
			LIST_REMOVE(r, res_list);
			/*@ +branchstate +onlytrans */
			free(r);
			r = r_tmp;
			continue;
		}
		diff_ts(&diff, &now, &(r->created)); 
		if (diff.tv_sec > 10) {
			/* Define three states: TIMEOUT, TSERROR and GOT_TS */
			if (r->state == STATE_GOT_PONG ||  r->state == STATE_READY) 
				r->state = STATE_TSERROR;
			else if (r->state == STATE_PING) 
				r->state = STATE_TIMEOUT;
			/* else: GOT_TS implicit */
			if (cfg.op == OPMODE_DAEMON) 
				if (write(cfg.fifo, (char*)r, sizeof *r) == -1)
					syslog(LOG_ERR, "daemon: write: %s", strerror(errno));
			if (cfg.op == OPMODE_CLIENT) { 
				if (r->state == STATE_TSERROR) {
					printf("Error    %4d from %d in %d sec (missing T2/T3)\n", 
							(int)r->seq, (int)r->id, (int)diff.tv_sec);
					res_tserror++;
				} else if (r->state == STATE_GOT_TS) {
					printf("Timeout  %4d from %d in %d sec (missing PONG)\n", 
							(int)r->seq, (int)r->id, (int)diff.tv_sec);
					res_pongloss++;
				} else {
					printf("Timeout  %4d from %d in %d sec (missing all)\n", 
							(int)r->seq, (int)r->id, (int)diff.tv_sec);
					res_timeout++;
				}
			}
			/* Timeout; safe removal */
			r_tmp = r->res_list.le_next;
			/*@ -branchstate -onlytrans TODO wtf */
			LIST_REMOVE(r, res_list);
			/*@ +branchstate +onlytrans */
			free(r);
			r = r_tmp;
			continue;
		}
		/* Alright, next entry */
		r = r->res_list.le_next;
	}
}

void client_res_summary(/*@unused@*/ int sig) {
	float loss;

	loss = (float)(res_timeout + res_pongloss) / 
			(float)(res_response + res_tserror + res_timeout + res_pongloss);
	loss = loss * 100;
	printf("\n%d ok, %d ts errors, %d lost pongs, %d timeouts, %f%% loss\n", 
			res_response, res_tserror, res_pongloss, res_timeout, loss);
	if (res_rtt_max.tv_sec > 0)
		printf("max: %ld.%09ld", res_rtt_max.tv_sec, res_rtt_max.tv_nsec);
	else 
		printf("max: %ld ns", res_rtt_max.tv_nsec);
	loss = (float)res_rtt_total / (float)res_response;
	printf(", avg: %.0f ns", loss);
	if (res_rtt_min.tv_sec > 0)
		printf(", min: %ld.%09ld\n", res_rtt_min.tv_sec, res_rtt_min.tv_nsec);
	else 
		printf(", min: %ld ns\n", res_rtt_min.tv_nsec);
	exit(0);
}
