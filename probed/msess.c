#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/queue.h>
#include <arpa/inet.h>
#include <sqlite3.h>

#include "probed.h"
#include "msess.h"

struct msess *sessions, *cur;
struct msess_head sessions_head;
struct timespec timeout;

/*
 * Initialize measurement session list handler
 */
void msess_init(void) {

	int rcode;
	char tmpstr[64];

	LIST_INIT(&sessions_head);

}

/*
 * Add measurement session to list
 */
struct msess *msess_add(msess_id id) {

	struct msess *s;

	s = malloc(sizeof (struct msess));
	memset(s, 0, sizeof (struct msess));
	s->id = id;

	LIST_INSERT_HEAD(&sessions_head, s, entries);
	
	msess_reset();

	return s;
	
}

/*
 * Remove measurement session from list
 */
void msess_remove(struct msess *sess) {

	/* remove msess */
	LIST_REMOVE(sess, entries);
	free(sess);

} 

/*
 * Find a msess entry for the given address and ID
 */
struct msess *msess_find(msess_id id) {

	struct msess *sess;

	/* iterate all sessions and compare IDs */
	for (sess = sessions_head.lh_first; sess != NULL; sess = sess->entries.le_next) {

		if (sess->id == id) {
			return sess;
		}

	}

	// no msess found
	return NULL;

}

/*
 * Prints all sessions currently configured to console
 */
void msess_print_all(void) {

	struct msess *sess;

	for (sess = sessions_head.lh_first; sess != NULL; sess = sess->entries.le_next) {
		msess_print(sess);
	}

}

/*
 * Print one session to console
 */
void msess_print(struct msess *sess) {

	char addr_str[INET6_ADDRSTRLEN];
	struct sockaddr_in6 *addr;

	addr = (struct sockaddr_in6 *)&(sess->dst);
	inet_ntop(AF_INET6, addr->sin6_addr.s6_addr, addr_str, INET6_ADDRSTRLEN);
	printf("Measurement session ID %d:\n", sess->id);
	printf(" Destination address: %s\n", addr_str);
	printf(" Destination port: %d\n", ntohs(addr->sin6_port));
	printf(" Interval: %d.%06d s\n", (int)sess->interval.tv_sec, (int)sess->interval.tv_usec);
	printf(" Last sequence number: %d\n", sess->last_seq);

	printf("\n");

}

/*
 * function to iterate over all msesses
 */
struct msess *msess_next(void) {

	struct msess *ret;

	ret = cur;
	cur = ret->entries.le_next;
	if (cur == NULL) {
		msess_reset();
	}

	return ret;

}

/*
 * reset msess iterator variable
 */
void msess_reset(void) {

	cur = sessions_head.lh_first;

}

/*
 * get sequence number (and increase counter)
 */
uint32_t msess_get_seq(struct msess *sess) {

	sess->id++;
	return sess->id;

}
