/*
 * util.c
 *
 * Contains various utilities.
 *
 */

#include <time.h>
#include "probed.h"


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

/*
 * Exit and log message
 */
void die(char *msg) {
	syslog(LOG_ERR, msg);
	exit(1);
}

/*
 * Enable debugging
 */
void debug(char enabled) {
	c.debug = enabled;
	if (enabled) setlogmask(LOG_UPTO(LOG_DEBUG));
	else setlogmask(LOG_UPTO(LOG_INFO));
	return;
}
