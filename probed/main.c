#include "sla-ng.h"

int                s;       /* bind socket */
unsigned int       yes = 1; /* usefull macro */
unsigned int       no = 0; /* usefull macro */
struct sockaddr_in6 them;    /* other side's ip address */
int                slen;    /* size of sockaddr_in */

int main(int argc, char *argv[]) {
	int syslog_flags = 0;
	int arg;

	/* command line arguments */
	while ((arg = getopt(argc, argv, "d")) != -1)
		if (arg == 'd') syslog_flags |= LOG_PERROR;
	/* syslog */
	openlog("probed", syslog_flags, LOG_USER);
	/* get initial xml config to *doc */
	config_init();
	/* macros */
	slen = sizeof(them);
	/* give us a dual-stack (ipv4/6) socket */
	s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	if (s < 0) {
		syslog(LOG_ERR, "socket: %s", strerror(errno));
		die("Could not create UDP socket.");
	} 
	if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &no, sizeof(no)) < 0)
		syslog(LOG_ERR, "setsockopt: IPV6_V6ONLY: %s", strerror(errno));
	/* read config, enabling timestamps */
	signal(SIGHUP, config_read);
	config_read();
	/* start the ping protocol loop */
	proto();
	/* close */
	close(s);
	closelog();
	return 0;
}

void die(char *msg) {
	syslog(LOG_ERR, msg);
	exit(1);
}

/* calculate nsec precision diff for positive time */
void diff_ts (struct timespec *r, struct timespec *end, struct timespec *beg) {
	if ((end->tv_nsec - beg->tv_nsec) < 0) {
		r->tv_sec = end->tv_sec - beg->tv_sec - 1;
		r->tv_nsec = 1000000000 + end->tv_nsec - beg->tv_nsec;
	} else {
		r->tv_sec = end->tv_sec - beg->tv_sec;
		r->tv_nsec = end->tv_nsec - beg->tv_nsec;
	}
}
/* calculate usec precision diff for positive time */
void diff_tv (struct timeval *r, struct timeval *end, struct timeval *beg) {
	if ((end->tv_usec - beg->tv_usec) < 0) {
		r->tv_sec = end->tv_sec - beg->tv_sec - 1;
		r->tv_usec = 1000000 + end->tv_usec - beg->tv_usec;
	} else {
		r->tv_sec = end->tv_sec - beg->tv_sec;
		r->tv_usec = end->tv_usec - beg->tv_usec;
	}
}
