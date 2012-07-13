/* 
 * Copyright (c) 2011 Anders Berggren, Lukas Garberg, Tele2
 *
 * We have not yet decided upon a license, and so far it may only be
 * used and redistributed with our explicit permission.
 */ 

/**
 * Contains various utilities.
 * 
 * \author Anders Berggren <anders@halon.se>
 * \author Lukas Garberg <lukas@spritelink.net>
 * \file util.c
 */

#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <arpa/inet.h>
#include "probed.h"

/** 
 * Create string containing IP address.
 *
 * Place human-readable IPv6 or IPv6-mapped-IPv4 address into
 * string 's' with size INET6_ADDRSTRLEN.
 *
 * \param[in] a addr_t to fetch address from.
 * \param[out] s Buffer where the string is written.
 * \return Status; 0 on success, <0 on failure.
 */
int addr2str(addr_t *a, /*@out@*/ char *s) {
	if (inet_ntop(AF_INET6, &(a->sin6_addr), s, INET6_ADDRSTRLEN) == NULL) {
		syslog(LOG_ERR, "inet_ntop: %s", strerror(errno));
		return -1;
	}
	return 0;
}

/**
 * Prints a string.
 *
 * Just print string, without respect to return value.
 *
 * \param[in] str String to print.
 */
void p(char *str) {
	(void)puts(str);
}

/**
 * Enable debug logging.
 *
 * Configures the syslog handler to enable/disable logging of debug
 * messages.
 *
 * \param[in] enabled Boolean to determine to enable or disable.
 */
void debug(int enabled) {
	if (enabled == 1) 
		(void)setlogmask(LOG_UPTO(LOG_DEBUG));
	else 
		(void)setlogmask(LOG_UPTO(LOG_INFO));
	return;
}

/*@ -type Disable fucked up 'Arrow access field of non-struct...' */
/**
 * Calculates the difference between two timespec.
 *
 * Calculates the value \p a - \p b.
 * 
 * \param[out] r Pointer to timespec where result will be written.
 * \param[in] a Time #1.
 * \param[in] b Time #2.
 * \return sign; 0 on positive, 1 on negative.
 */
int diff_ts (/*@out@*/ ts_t *r, ts_t *a, ts_t *b) {

	int neg = 0;

	memset(r, 0, sizeof *r);

	/*
	 * This is a very long and straight-forward implementation.
	 * Can (should? :)) be optimized!
	 */

	/* seconds larger? */
	if (a->tv_sec > b->tv_sec) {

		r->tv_sec = a->tv_sec - b->tv_sec;

		/* nanoseconds larger? */
		if (a->tv_nsec > b->tv_nsec) {

			r->tv_nsec = a->tv_nsec - b->tv_nsec;

		/* nanoseconds smaller? */
		} else if (a->tv_nsec < b->tv_nsec) {

			r->tv_sec -= 1;
			r->tv_nsec = 1000000000 + a->tv_nsec - b->tv_nsec;

		/* nanoseconds equal */
		} else {

			r->tv_nsec = 0;

		}

	/* seconds smaller? */
	} else if (a->tv_sec < b->tv_sec) {

		neg = 1;
		r->tv_sec = b->tv_sec - a->tv_sec;

		/* nanoseconds larger? */
		if (a->tv_nsec > b->tv_nsec) {

			r->tv_sec -= 1;
			r->tv_nsec = b->tv_nsec + 1000000000 - a->tv_nsec;

		/* nanoseconds smaller? */
		} else if (a->tv_nsec < b->tv_nsec) {

			r->tv_nsec = b->tv_nsec - a->tv_nsec;

		/* nanoseconds equal */
		} else {

			r->tv_nsec = 0;

		}

	/* seconds equal */
	} else {

		r->tv_sec = 0;

		/* nanoseconds larger? */
		if (a->tv_nsec > b->tv_nsec) {

			r->tv_nsec = a->tv_nsec - b->tv_nsec;

		} else if (a->tv_nsec < b->tv_nsec) {

			neg = 1;
			r->tv_nsec = b->tv_nsec - a->tv_nsec;

		} else {

			r->tv_nsec = 0;

		}

	}

	return neg;

}

/** 
 * Compare two timespec.
 *
 * Returns -1 if \p t1 < \p t2, 1 if \p t1 > \p t2 and 0 if \p t1 == \p t2
 *
 * \param[in] t1 Time #1.
 * \param[in] t2 Time #2.
 * \return Comparison result.
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
