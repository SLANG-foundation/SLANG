/*
 * Contains various utilities.
 */

#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include "probed.h"

/* 
 * Place human-readable IPv6 or IPv6-mapped-IPv4 address into
 * string 's' with size INET6_ADDRSTRLEN.
 */
int addr2str(addr_t *a, /*@out@*/ char *s) {
	if (inet_ntop(AF_INET6, &(a->sin6_addr), s, INET6_ADDRSTRLEN) == NULL) {
		syslog(LOG_ERR, "inet_ntop: %s", strerror(errno));
		return -1;
	}
	return 0;
}

/*
 * Just print 'str' content, without respect to return value.
 */
void p(char *str) {
	(void)puts(str);
}

void debug(int enabled) {
	if (enabled == 1) (void)setlogmask(LOG_UPTO(LOG_DEBUG));
	else (void)setlogmask(LOG_UPTO(LOG_INFO));
	return;
}

/*@ -type Disable fucked up 'Arrow access field of non-struct...' */
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

/* 
 * compare two timespecs 
 *
 * Returns -1 if t1 < t2, 1 if t1 > t2 and 0 if t1 == t2
 */
int cmp_ts(struct timespec *t1, struct timespec *t2) {

	if (t1->tv_sec < t2->tv_sec) {
		return -1;
	} else if (t1->tv_sec > t2->tv_sec) {
		return 1;
	} else if (t1->tv_nsec < t2->tv_nsec) {
		return -1;
	} else if (t1->tv_nsec > t2->tv_nsec) {
		return 1;
	} else { /* equal */
		return 0;
	}

}

/* 
 * compare two timevals 
 *
 * Returns -1 if t1 < t2, 1 if t1 > t2 and 0 if t1 == t2
 */
int cmp_tv(struct timeval *t1, struct timeval *t2) {

	if (t1->tv_sec < t2->tv_sec) {
		return -1;
	} else if (t1->tv_sec > t2->tv_sec) {
		return 1;
	} else if (t1->tv_usec < t2->tv_usec) {
		return -1;
	} else if (t1->tv_usec > t2->tv_usec) {
		return 1;
	} else { /* equal */
		return 0;
	}

}
