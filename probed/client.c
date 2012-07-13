/*
 * Copyright (c) 2011 Anders Berggren, Lukas Garberg, Tele2
 *
 * We have not yet decided upon a license, and so far it may only be
 * used and redistributed with our explicit permission.
 */

/**
 * \file   client.c
 * \brief  Contains 'client' (PING) specific code, and result handling
 * \author Anders Berggren <anders@halon.se>
 * \author Lukas Garberg <lukas@spritelink.net>
 * \date   2011-01-10
 * \todo   Lots of LINT (splint) warnings that I don't understand
 */

#include <stdlib.h>
#ifndef S_SPLINT_S /* SPlint 3.1.2 bug */
#include <unistd.h>
#endif
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <fcntl.h>
#include <syslog.h>
#include <netdb.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include "probed.h"
#include "client.h"
#include "util.h"
#include "unix.h"
#include "net.h"

#define MASK_PING 1 /* Got ping */
#define MASK_PONG 2 /* Got pong */
#define MASK_TIME 4 /* Got timestamp */
#define MASK_DONE 7 /* Got everything */
#define MASK_DSCP 8 /* DSCP error occured */

#define STATE_SUCCESS  1
#define STATE_DS_ERR 2
#define STATE_TS_ERR 3
#define STATE_PONGLOSS 4
#define STATE_TIMEOUT 5
#define STATE_DUP 6

#define XML_NODE "probe"

/* List of probe results */
struct res {
	/*@dependent@*/ ts_t created;
	int state;
	struct in6_addr addr;
	num_t id;
	num_t seq;
	/*@dependent@*/ ts_t ts[4];
	TAILQ_ENTRY(res) list;
};

/**
 * Struct for storing configuration for one measurement session.
 */
struct msess {
	num_t id; /**< Measurement session ID */
	TAILQ_HEAD(res_listhead, res) res_head;
	addr_t dst; /**< Destination address and port */
	int msec_interval; /**< Probe interval */
	int timeout; /**< Timeout for PING */
	int got_hello; /**< Are we connected with server? */
	uint8_t dscp; /**< DiffServ Code Point value of measurement session */
	pid_t child_pid; /**< PID of child process doing the TCP connection */
	uint32_t last_seq; /**< Last sequence number sent */
	LIST_ENTRY(msess) list;
};

static LIST_HEAD(msess_listhead, msess) msess_head;

struct res_fifo {
	uint32_t id;
	uint32_t seq;
	uint32_t state;
	uint32_t created_sec;
	uint32_t created_nsec;
	uint32_t rtt_sec;
	uint32_t rtt_nsec;
};
struct fifoq {
	struct res_fifo res;
	TAILQ_ENTRY(fifoq) list;
};
static TAILQ_HEAD(fifoq_listhead, fifoq) fifoq_head;
/* Client mode statistics */
static int res_ok = 0;
static int res_timeout = 0;
static int res_pongloss = 0;
static int res_tserror = 0;
static int res_dserror = 0;
static int res_dup = 0;
static long long res_rtt_total = 0;
static ts_t res_rtt_min, res_rtt_max;

static void client_res_insert(addr_t *a, data_t *d, ts_t *ts);
static pid_t client_fork(int pipe, addr_t *server);
static int client_msess_isaddrtaken(addr_t *addr, num_t id);
static void client_write_fifo(struct res_fifo *r_fifo);

/**
 * Initializes global variables
 *
 * Init global variables such as the linked lists. Should be run once.
 */
void client_init(void) {
	/*@ -mustfreeonly -immediatetrans TODO wtf */
	LIST_INIT(&msess_head);
	TAILQ_INIT(&fifoq_head);
	/*@ +mustfreeonly +immediatetrans */
	res_rtt_min.tv_sec = -1;
	res_rtt_min.tv_nsec = 0;
	res_rtt_max.tv_sec = 0;
	res_rtt_max.tv_nsec = 0;
	/*@ -nullstate TODO wtf? */
	return;
	/*@ +nullstate */
}

/**
 * Initializes a FIFO used by client_res_* functions in DAEMON mode
 *
 * When running in deamon mode, open a FIFO. Should be run once.
 *
 * @todo Is the really good to wait for FIFO open?
 */
void client_res_fifo_or_die(char *fifopath) {
	(void)unlink(fifopath);
	if (mknod(fifopath, (__mode_t)S_IFIFO | 0644, (__dev_t)0) < 0) {
		syslog(LOG_ERR, "mknod: %s: %s", fifopath, strerror(errno));
		exit(EXIT_FAILURE);
	}
	syslog(LOG_INFO, "Waiting for listeners on FIFO %s", fifopath);
	cfg.fifo = open(fifopath, O_WRONLY);

	if (fcntl(cfg.fifo, F_SETFL, O_NONBLOCK) < 0) {
		syslog(LOG_ERR, "fcntl: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

}

/**
 * Fork a process which triggers the main proces at regular intervals
 *
 * To avoid keeping track of time in the main process, a fork is created which
 * handles this. For IPC there is a pipe, to which data is sent at regular
 * intervals (SEND_INTERVAL microseconds).
 *
 * \param pipe	IPC pipe to which trigger messages are sent
 */
void client_send_fork(int pipe) {

	int niter = 0;
	int time_dev = 0;
	int t_dev = 0;
	int neg;
	char b = 's';
	ts_t last, now, time_passed, diff, ideal;

	/* ideal time between timer checks
	 * the number '1000' is there to convert bewtween usec and nsec.
	 */
	ideal.tv_sec = 0;
	ideal.tv_nsec = SEND_INTERVAL *
		SEND_INTERVAL_DEV_INTERVAL * 1000;

	/* Do not react to SIGCHLD (when a child dies) */
	if (signal(SIGCHLD, SIG_IGN) == SIG_ERR)
		syslog(LOG_ERR, "send: signal: SIG_IGN on SIGCHLD failed");

	/* Create client fork; parent returns */
	if (fork() != 0) return;

	/* We are child, do not react to HUP (reload), INT (print) */
	if (signal(SIGHUP, SIG_IGN) == SIG_ERR)
		syslog(LOG_ERR, "send: signal: SIG_IGN on SIGHUP failed");
	if (signal(SIGINT, SIG_IGN) == SIG_ERR)
		syslog(LOG_ERR, "send: signal: SIG_IGN on SIGINT failed");

	/* Please kill me, I hate myself */
	(void)prctl(PR_SET_PDEATHSIG, SIGKILL);


	/*
	 * Infinite loop - this is where it happens.
	 * But first, initialize last iteration timestamp.
	 */
	(void)clock_gettime(CLOCK_REALTIME, &last);
	while (1) {

		usleep(SEND_INTERVAL - time_dev);

		/* send trigger message */
		if (write(pipe, &b, sizeof b) < 0)
			syslog(LOG_ERR, "send: write: %s", strerror(errno));

		/*
		 * The SEND_INTERVAL needs to be adjusted slightly as other
		 * actions in the loop takes some time to complete.
		 * Check this evere SEND_INTERVAL_DEV_INTERVAL:th iteration.
		 *
		 */
		if (niter % SEND_INTERVAL_DEV_INTERVAL == 0) {

			/*
			 * How long time did the last
			 * SEND_INTERVAL_DEV_INTERVAL iterations take?
			 */
			(void)clock_gettime(CLOCK_REALTIME, &now);
			diff_ts(&time_passed, &now, &last);
			memcpy(&last, &now, sizeof last);
			neg = diff_ts(&diff, &time_passed, &ideal);

			/*
			 * Calculate deviation from ideal time.
			 * Division by 1000 is conversion from nsec to usec.
			 */
			t_dev = diff.tv_nsec / 1000 /
				SEND_INTERVAL_DEV_INTERVAL;
			if (neg) {
				time_dev -= t_dev;
			} else {
				time_dev += t_dev;
			}

			/* Warn if we have very high values
			* and limit the modifier */
			if (time_dev > 300 || time_dev < -300) {

				syslog(LOG_INFO, "time diff large @%d: s: %d ns: %d; time_dev: %d",
					niter, (int)diff.tv_sec, (int)diff.tv_nsec, time_dev);
				syslog(LOG_INFO, "time_passed: s: %d ns: %d; ideal: s: %d ns: %d",
					(int)time_passed.tv_sec, (int)time_passed.tv_nsec,
					(int)ideal.tv_sec, (int)ideal.tv_nsec);

				if (time_dev > 300) {
					time_dev = 300;
				} else {
					time_dev = -300;
				}

			}

		}

		niter++;

	}

}


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
pid_t client_fork(int pipe, addr_t *server) {
	int sock, r;
	pid_t client_pid;
	char addrstr[INET6_ADDRSTRLEN];
	pkt_t pkt;
	fd_set fs;
	struct timeval tv;
	char log[100];
	ts_t zero;
	socklen_t slen;

	if (addr2str(server, addrstr) < 0)
		return -1;
	(void)snprintf(log, 100, "client: %s:", addrstr);
	/* Do not react to SIGCHLD (when a child dies) */
	if (signal(SIGCHLD, SIG_IGN) == SIG_ERR)
		syslog(LOG_ERR, "%s signal: SIG_IGN on SIGCHLD failed", log);
	/* Create client fork; parent returns */
	client_pid = fork();
	if (client_pid != 0) return client_pid;
	/* We are child, do not react to HUP (reload), INT (print) */
	if (signal(SIGHUP, SIG_IGN) == SIG_ERR)
		syslog(LOG_ERR, "%s signal: SIG_IGN on SIGHUP failed", log);
	if (signal(SIGINT, SIG_IGN) == SIG_ERR)
		syslog(LOG_ERR, "%s signal: SIG_IGN on SIGINT failed", log);
	/* Please kill me, I hate myself */
	(void)prctl(PR_SET_PDEATHSIG, SIGKILL);
	/* We're going to send a struct packet over the pipe */
	memset(&pkt.ts, 0, sizeof zero);
	/* Try to stay connected to server; forever */
	while (1 == 1) {
		memcpy(&pkt.addr, server, sizeof pkt.addr);
		if (addr2str(&pkt.addr, addrstr) < 0) {
			(void)sleep(10);
			continue;
		}
		syslog(LOG_INFO, "%s Connecting to port %d", log,
				ntohs(server->sin6_port));
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
 * Insert a new 'ping' into the result list
 *
 * Should be run once for each 'ping', inserting timestamp T1.
 *
 * \param a    The server IP address that is being pinged
 * \param d    The ping data, such as sequence number, session ID, etc.
 * \param ts   Pointer to the timestamp T1
 */
void client_res_insert(addr_t *a, data_t *d, ts_t *ts) {
	struct res *r;
	struct msess *s, *s2;

	s2 = NULL;
	for (s = msess_head.lh_first; s != NULL; s = s->list.le_next)
		if (s->id == d->id)
			s2 = s;
	if (s2 == NULL) return;
	r = malloc(sizeof *r);
	if (r == NULL) return;
	memset(r, 0, sizeof *r);
	(void)clock_gettime(CLOCK_REALTIME, &r->created);
	r->state = MASK_PING;
	memcpy(&r->addr, &a->sin6_addr, sizeof r->addr);
	r->id = d->id;
	r->seq = d->seq;
	r->ts[0] = *ts;
	/*@ -mustfreeonly -immediatetrans TODO wtf */
	TAILQ_INSERT_HEAD(&s2->res_head, r, list);
	/*@ +mustfreeonly +immediatetrans */
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
void client_res_update(addr_t *a, data_t *d, /*@null@*/ ts_t *ts, int dscp) {
	struct res *r;
	struct msess *s;
	struct res_fifo r_fifo;
	ts_t now, diff, rtt;
	int i, neg1 = 0, neg2 = 0, neg3 = 0;

	r = NULL;
	for (s = msess_head.lh_first; s != NULL; s = s->list.le_next)
		if (s->id == d->id)
			break;
	if (s == NULL)
		return;
	r = s->res_head.tqh_first;
	count_client_find = 0;
	while (r != NULL) {
		count_client_find++;
		/* If match; update */
		if (r->id == d->id &&
			r->seq == d->seq &&
			memcmp(&r->addr, &a->sin6_addr, sizeof r->addr) == 0) {
			if (d->type == TYPE_PONG) {
				r->state |= MASK_PONG;
				/* Save T4 timestamp */
				if (ts != NULL)
					r->ts[3] = *ts;
				/* DSCP failure status */
				if (s->dscp != (uint8_t)dscp)
					r->state |= MASK_DSCP;
			} else if (d->type == TYPE_TIME) {
				r->state |= MASK_TIME;
				r->ts[1] = d->t2;
				r->ts[2] = d->t3;
			}
			/* Update the status mask to status codes */
			if ((r->state & MASK_DONE) == MASK_DONE) {
				r_fifo.id = (uint32_t)r->id;
				r_fifo.seq = (uint32_t)r->seq;
				r_fifo.created_sec = (uint32_t)r->created.tv_sec;
				r_fifo.created_nsec = (uint32_t)r->created.tv_nsec;
				/* Check for DSCP error */
				if (r->state & MASK_DSCP)
					r_fifo.state = STATE_DS_ERR;
				else
					r_fifo.state = STATE_SUCCESS;

				/* Calculate RTT */
				neg1 = diff_ts(&diff, &r->ts[3], &r->ts[0]);
				neg2 = diff_ts(&now, &r->ts[2], &r->ts[1]);
				neg3 = diff_ts(&rtt, &diff, &now);
				r_fifo.rtt_sec = (uint32_t)rtt.tv_sec;
				r_fifo.rtt_nsec = (uint32_t)rtt.tv_nsec;

				/* Check that RTT calculations did not result in any
				 * negative numbers */
				if (neg1 || neg2 || neg3) {
					r_fifo.state = STATE_TS_ERR;
					syslog(LOG_ERR, "RTT calculation resulted in negative "
						"value: neg1 %d neg2 %d neg3 %d\n", neg1, neg2, neg3);
				}

				/* Check that all timestamps are present */
				for (i = 0; i < 4; i++)
					if (r->ts[i].tv_sec == 0 && r->ts[i].tv_nsec == 0)
						r_fifo.state = STATE_TS_ERR;
				if (r_fifo.state == STATE_SUCCESS &&
						r_fifo.rtt_sec > 20) {
					syslog(LOG_ERR, "Strange RTT %d %ld.%ld sec\n",
					      r->id, rtt.tv_sec, rtt.tv_nsec);
						r_fifo.state = STATE_TS_ERR;
				}
				count_client_done++;

				/* Pipe (daemon) output */
				if (cfg.op == DAEMON)
					client_write_fifo(&r_fifo);
				/* Client output */
				if (cfg.op == CLIENT) {
					if (r_fifo.state == STATE_TS_ERR) {
						res_tserror++;
						printf("Error    %4d from %d (invalid timestamps)\n",
								(int)r->seq, (int)r->id);
					} else if (r_fifo.state == STATE_DS_ERR) {
						res_dserror++;
						printf("Error    %4d from %d in %d sec (invalid DSCP)\n",
								(int)r->seq, (int)r->id, (int)diff.tv_sec);
					} else { /* STATE_SUCCESS implicit */
						res_ok++;
						neg1 = diff_ts(&diff, &r->ts[3], &r->ts[0]);
						neg2 = diff_ts(&now, &r->ts[2], &r->ts[1]);
						neg3 = diff_ts(&rtt, &diff, &now);
						if (neg1 || neg2 || neg3) {
							printf("RTT calculation resulted in negative "
								"value: neg1 %d neg2 %d neg3 %d\n",
								neg1, neg2, neg3);
						}
						if (rtt.tv_sec > 0)
							printf("Response %4d from %d in %10ld.%09ld\n",
									(int)r->seq, (int)r->id, rtt.tv_sec,
									rtt.tv_nsec);
						else
							printf("Response %4d from %d in %ld ns\n",
									(int)r->seq, (int)r->id, rtt.tv_nsec);
						if (cmp_ts(&res_rtt_max, &rtt) == -1)
							res_rtt_max = rtt;
						if (res_rtt_min.tv_sec == -1)
							res_rtt_min = rtt;
						if (cmp_ts(&res_rtt_min, &rtt) == 1)
							res_rtt_min = rtt;
						res_rtt_total = res_rtt_total + rtt.tv_nsec;
					}
				}
				/*@ -branchstate -onlytrans TODO wtf */
				TAILQ_REMOVE(&s->res_head, r, list);
				/*@ +branchstate +onlytrans */
				free(r);
			}
			return;
		}
		/* Alright, next entry */
		r = r->list.tqe_next;
	}
	/* Didn't find PING. DUP! */
	i = 0;
	for (s = msess_head.lh_first; s != NULL; s = s->list.le_next)
		if (memcmp(&s->dst.sin6_addr, &a->sin6_addr, sizeof a->sin6_addr) == 0)
			if (d->type == TYPE_PONG)
				if (s->id == d->id)
					if (s->last_seq >= d->seq)
						i = 1;
	/* DUPs should not come from the future :) Reconf? */
	if (i == 0) return;
	memset(&r_fifo, 0, sizeof r_fifo);
	(void)clock_gettime(CLOCK_REALTIME, &now);
	r_fifo.state = STATE_DUP;
	r_fifo.id = (uint32_t)d->id;
	r_fifo.seq = (uint32_t)d->seq;
	r_fifo.created_sec = (uint32_t)now.tv_sec;
	r_fifo.created_nsec = (uint32_t)now.tv_nsec;
	if (cfg.op == DAEMON)
		client_write_fifo(&r_fifo);
	if (cfg.op == CLIENT) {
		res_dup++;
		printf("Unknown  %4d from %d (probably DUP)\n",
				(int)d->seq, (int)d->id);
	}
}

void client_res_summary(/*@unused@*/ int sig) {
	float loss;
	long long total;

	total = (res_ok + res_dserror + res_tserror + res_timeout + res_pongloss);
	loss = (float)(res_timeout + res_pongloss) / (float)total;
	loss = loss * 100;
	printf("\n");
	printf("%d ok, %d dscp errors, %d ts errors, %d unknown/dups\n",
			res_ok, res_dserror, res_tserror, res_dup);
	printf("%d lost pongs, %d timeouts, %f%% loss\n",
			res_pongloss, res_timeout, loss);
	if (res_rtt_max.tv_sec > 0)
		printf("max: %ld.%09ld", res_rtt_max.tv_sec, res_rtt_max.tv_nsec);
	else
		printf("max: %ld ns", res_rtt_max.tv_nsec);
	loss = (float)res_rtt_total / (float)res_ok;
	printf(", avg: %.0f ns", loss);
	if (res_rtt_min.tv_sec > 0)
		printf(", min: %ld.%09ld\n", res_rtt_min.tv_sec, res_rtt_min.tv_nsec);
	else
		printf(", min: %ld ns\n", res_rtt_min.tv_nsec);
	exit(0);
}

void client_res_clear_timeouts(void) {
	struct res *r, *r_tmp;
	struct res_fifo r_fifo;
	struct msess *s;
	ts_t now, diff;

	(void)clock_gettime(CLOCK_REALTIME, &now);
	r = NULL;
	/* For each mesasurement sesion */
	for (s = msess_head.lh_first; s != NULL; s = s->list.le_next) {
		/* For each probe, in reverse order */
		if ((&s->res_head)->tqh_last == NULL)
			continue;
		r = *(((struct res_listhead *)((&s->res_head)->tqh_last))->tqh_last);
		while (r != NULL) {
			diff_ts(&diff, &now, &r->created);
			if (diff.tv_sec <= TIMEOUT) {
				/* We have reached young probes; stop looking */
				break;
			} else {
				/*
				 * Define three states:
				 * PONGLOSS, we have a TCP timestamp, but no pong
				 * TS_ERR, we have a pong, but no TCP timestamp
				 * TIMEOUT, we have nothing
				 */
				memset(&r_fifo, 0, sizeof r_fifo);
				if (r->state & MASK_TIME)
					r_fifo.state = STATE_PONGLOSS;
				else if (r->state & MASK_PONG)
				   	r_fifo.state = STATE_TS_ERR;
				else /* MASK_PING implicit */
					r_fifo.state = STATE_TIMEOUT;
				r_fifo.id = (uint32_t)r->id;
				r_fifo.seq = (uint32_t)r->seq;
				r_fifo.created_sec = (uint32_t)r->created.tv_sec;
				r_fifo.created_nsec = (uint32_t)r->created.tv_nsec;
				count_client_done++;
				if (cfg.op == DAEMON)
					client_write_fifo(&r_fifo);
				/* Client output */
				if (cfg.op == CLIENT) {
					if (r_fifo.state == STATE_TS_ERR) {
						res_tserror++;
						printf("Error    %4d from %d in %d sec (missing T2/T3)\n",
								(int)r->seq, (int)r->id, (int)diff.tv_sec);
					} else if (r_fifo.state == STATE_PONGLOSS) {
						res_pongloss++;
						printf("Timeout  %4d from %d in %d sec (missing PONG)\n",
								(int)r->seq, (int)r->id, (int)diff.tv_sec);
					} else if (r_fifo.state == STATE_TIMEOUT) {
						res_timeout++;
						printf("Timeout  %4d from %d in %d sec (missing all)\n",
								(int)r->seq, (int)r->id, (int)diff.tv_sec);
					} else {
						printf("Error    %4d from %d (unknown error)\n",
								(int)r->seq, (int)r->id);
					}
				}
				/* Ready, timeout or error; safe removal */
				r_tmp = *(((struct res_listhead *)(r->list.tqe_prev))->tqh_last);
				/*@ -branchstate -onlytrans TODO wtf */
				TAILQ_REMOVE(&s->res_head, r, list);
				/*@ +branchstate +onlytrans */
				free(r);
				r = r_tmp;
				continue;
			}
			r = *(((struct res_listhead *)(r->list.tqe_prev))->tqh_last);
		}
	}
	return;
}

void client_write_fifo(struct res_fifo *r_fifo) {
	struct fifoq *q, *q_tmp;

	q = fifoq_head.tqh_first;
	while (q != NULL) {
		q_tmp = q->list.tqe_next;
		if (write(cfg.fifo, (char *)&q->res, sizeof q->res) == -1) {
			break;
		} else {
			TAILQ_REMOVE(&fifoq_head, q, list);
			free(q);
			q = q_tmp;
			count_client_fifoq--;
		}
	}

	if (write(cfg.fifo, (char *)r_fifo, sizeof *r_fifo) == -1) {
		q = malloc(sizeof *q);
		if (q == NULL) return;
		//memset(q, 0, sizeof *q);
		memcpy(&q->res, r_fifo, sizeof *r_fifo);
		TAILQ_INSERT_TAIL(&fifoq_head, q, list);
		count_client_fifoq++;
		count_client_fifoq_max = MAX(count_client_fifoq_max,
				count_client_fifoq);
	}
	//syslog(LOG_ERR, "daemon: write: %s", strerror(errno));
}

/**
 * Insert measurement session to list, used mainly by client mode
 *
 * Whereas DAEMON mode uses client_msess_reconf to populate msess,
 * this code is mainly for the CLIENT mode, in order to add a measurement
 * session.
 *
 * \return 0 on success, otherwise -1
 */
int client_msess_add(char *port, char *a, uint8_t dscp, int wait, num_t id) {
	int ret;
	struct msess *s;
	struct addrinfo /*@dependent@*/ dst_hints, *dst_addr;

	s = malloc(sizeof *s);
	if (s == NULL) return -1;
	memset(s, 0, sizeof *s);
	s->id = id;
	s->dscp = dscp;
	s->msec_interval = wait;
	/* Prepare for getaddrinfo */
	memset(&dst_hints, 0, sizeof dst_hints);
	dst_hints.ai_family = AF_INET6;
	dst_hints.ai_flags = AI_V4MAPPED;
	ret = getaddrinfo(a, port, &dst_hints, &dst_addr);
	if (ret < 0) {
		syslog(LOG_ERR, "Unable to look up hostname %s: %s", a,
				gai_strerror(ret));
		free(s);
		return -1;
	}
	memcpy(&s->dst, dst_addr->ai_addr, sizeof s->dst);
	freeaddrinfo(dst_addr);
	/*@ -mustfreeonly -immediatetrans TODO wtf */
	LIST_INSERT_HEAD(&msess_head, s, list);
	/*@ +mustfreeonly +immediatetrans */
	/*@ -compmempass TODO wtf? */
	return 0;
	/*@ +compmempass */
}

/**
 * Send PING packets on the UDP socket for all measurement sessions
 *
 * This is called from the main loop at constant intervals, and
 * this function determines if it is time to send to a particular
 * msess
 *
 * \param[in] s_udp The UDP socket to send on
 */

void client_msess_transmit(int s_udp, int sends) {
	struct msess *s;
	data_t tx;
	ts_t ts;
	
	for (s = msess_head.lh_first; s != NULL; s = s->list.le_next) {
		/* Are we connected to server? */
		if (s->got_hello != 1)
			continue;
		/* time to send new packet? */
		if (s->msec_interval < 1) {
			syslog(LOG_CRIT, "Invalid interval");
			continue;
		}
		if (sends % s->msec_interval == 0) {
			count_client_sent++;
			memset(&tx, 0, sizeof tx);
			tx.type = TYPE_PING;
			tx.id = s->id;
			s->last_seq++;
			tx.seq = s->last_seq;
			last_tx_id = s->id;
			last_tx_seq = s->last_seq;
			(void)dscp_set(s_udp, s->dscp);
			if (send_w_ts(s_udp, &s->dst, (char*)&tx, &ts) < 0)
				syslog(LOG_INFO, "skipping send");
			else
				client_res_insert(&s->dst, &tx, &ts);
		}
	}

}

/**
 * Spawn client forks for all configured measurement sessions
 *
 * See comments on client_fork for more information.
 *
 * \param[in] pipe The pipe file descriptor where results are sent
 */
void client_msess_forkall(int pipe) {
	struct msess *s;

	for (s = msess_head.lh_first; s != NULL; s = s->list.le_next) {
		/* Make sure there is no fork already running with
		 * the same destination address */
		if (client_msess_isaddrtaken(&s->dst, s->id) == 1) {
			continue;
		}
		s->child_pid = client_fork(pipe, &s->dst);
	}
}

/**
 * Reload configuration; measurement sessions in DAEMON mode
 *
 * Mostly, this is about:
 * 1. Killing all client forks (children handling the TCP)
 * 2. Empty the msess list (measurement sessions)
 * 3. Empty the result list (measurement results)
 * 4. Re-populate msess from XML configuration file
 * 5. Start client forks again (not done here!)
 *
 * \param[in] port    Because getaddrinfo needs the "global" port
 * \param[in] cfgpath We need the path to the XML file
 * \return            0 on success, -1 on error
 */

int client_msess_reconf(char *port, char *cfgpath) {
	int ok, ret = 0;
	struct msess *s, *s_tmp;
	struct res *r, *r_tmp;
	struct addrinfo /*@dependent@*/ dst_hints, *dst_addr;
	xmlDoc *cfgdoc = 0;
	xmlNode *root, *n, *k;
	xmlChar *c;

	syslog(LOG_INFO, "Reloading configuration...");
	/* Sanity check; only run this if in DAEMON mode */
	if (cfg.op != DAEMON)
		return -1;
	cfgdoc = xmlParseFile(cfgpath);
	/* Configuration sanity checks */
	if (!cfgdoc) {
		syslog(LOG_ERR, "No configuration");
		return -1;
	}
	root = cfgdoc->children;
	if (!root) {
		syslog(LOG_ERR, "Empty configuration");
		xmlFreeDoc(cfgdoc);
		/*@ -mustfreefresh TODO SPlint does not understand xmlFree? */
		return -1;
		/*@ +mustfreefresh */
	}
	/* Kill all clients */
	s = msess_head.lh_first;
	while (s != NULL) {
		/* Kill child process */
		if (s->child_pid != 0)
			if (kill(s->child_pid, SIGKILL) != 0)
				syslog(LOG_ERR, "client: kill: %s", strerror(errno));
		/* Kill all client results */
		r = s->res_head.tqh_first;
		while (r != NULL) {
			r_tmp = r->list.tqe_next;
			/*@ -branchstate -onlytrans TODO wtf */
			TAILQ_REMOVE(&s->res_head, r, list);
			/*@ +branchstate +onlytrans */
			free(r);
			r = r_tmp;
		}
		s_tmp = s->list.le_next;
		/*@ -branchstate -onlytrans TODO wtf */
		LIST_REMOVE(s, list);
		/*@ +branchstate +onlytrans */
		free(s);
		s = s_tmp;
	}
	/*@ -mustfreeonly -immediatetrans TODO wtf */
	LIST_INIT(&msess_head);
	/*@ +mustfreeonly +immediatetrans */
	/* Populate msess list from config */
	for (n = root->children; n != NULL; n = n->next) {
		/* Begin <probe> loop */
		if (n->type != XML_ELEMENT_NODE)
			continue;
		if (strncmp((char *)n->name, XML_NODE, strlen(XML_NODE)) != 0)
			continue;
		/* Get ID */
		c = xmlGetProp(n, (xmlChar *)"id");
		if (c == NULL) {
			syslog(LOG_ERR, "Probe is missing id=");
			continue;
		}
		s = malloc(sizeof *s);
		if (s == NULL) continue;
		memset(s, 0, sizeof *s);
		s->id = (num_t)atoi((char *)c);
		xmlFree(c);
		s->msec_interval = 1000;
		ok = 0;
		for (k = n->children; k != NULL; k = k->next) {
			/* Begin <address/dscp/etc> loop */
			if (k->type != XML_ELEMENT_NODE)
				continue;
			/*@ -mustfreefresh TODO Doesn't understand xmlFree */
			c = xmlNodeGetContent(k);
			/*@ +mustfreefresh */
			/* Interval */
			if (strcmp((char *)k->name, "interval") == 0)
				s->msec_interval = atoi((char *)c);
			/* Address */
			if (strcmp((char *)k->name, "address") == 0) {
				/* prepare for getaddrinfo */
				memset(&dst_hints, 0, sizeof dst_hints);
				dst_hints.ai_family = AF_INET6;
				dst_hints.ai_flags = AI_V4MAPPED;
				ret = getaddrinfo((char *)c, port, &dst_hints, &dst_addr);
				if (ret < 0) {
					syslog(LOG_ERR, "Probe hostname %s: %s", (char *)c,
							gai_strerror(ret));
				} else {
					ok = 1;
					memcpy(&s->dst, dst_addr->ai_addr, sizeof s->dst);
					freeaddrinfo(dst_addr);
				}

			}
			/* DSCP */
			if (strcmp((char *)k->name, "dscp") == 0)
				s->dscp = (uint8_t)atoi((char *)c);
			xmlFree(c);
			/* End <address/dscp/etc> loop */
		}
		/*@ -mustfreeonly -immediatetrans TODO wtf */
		if (ok == 1) {
			TAILQ_INIT(&s->res_head);
			LIST_INSERT_HEAD(&msess_head, s, list);
		} else {
			free(s);
		}
		/*@ +mustfreeonly +immediatetrans */
		/* End <probe> loop */
		/*@ -branchstate -mustfreefresh TODO wtf */
	}
	/*@ +branchstate */
	/*@ -compmempass -nullstate -mustfreefresh xmlFree */
	xmlFreeDoc(cfgdoc);
	return 0;
	/*@ +compmempass +mustfreefresh +nullstate */
}

/**
 * Set the measurement session with address 'addr' to 'ready'
 *
 * \param[in] addr Address of the connected session
 * \return         Returns 0 is the address was found
 */
int client_msess_gothello(addr_t *addr) {
	size_t slen;
	struct msess *s;
	int ok = 0;

	slen = sizeof s->dst.sin6_addr;
	for (s = msess_head.lh_first; s != NULL; s = s->list.le_next) {
		if (memcmp(&addr->sin6_addr, &s->dst.sin6_addr, slen) == 0) {
			s->got_hello = 1;
			ok = 1;
		}
	}
	if (ok == 1)
		return 0;
	return -1;
}

/**
 * Find msess with running child process bound to a certain address
 *
 * \param[in] addr The address to look for
 * \return         1 if it was found, otherwise 0
 */
int client_msess_isaddrtaken(addr_t *addr, num_t id) {
	struct msess *s;
	size_t len;

	for (s = msess_head.lh_first; s != NULL; s = s->list.le_next) {
		if (s->id == id)
			continue;
		if (s->child_pid == 0)
			continue;
		/* compare addresses */
		len = sizeof s->dst.sin6_addr;
		if (memcmp(&addr->sin6_addr, &s->dst.sin6_addr, len) == 0)
			return 1;
	}
	return 0;
}
