#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/queue.h>
#include <arpa/inet.h>

#include "probed.h"
#include "msess.h"

struct msess *sessions;
struct msess_head sessions_head;

/*
 * Initialize measurement session list
 */
void msess_init(void) {

	LIST_INIT(&sessions_head);

}

/*
 * Add measurement session to list
 */
struct msess *msess_add(void) {

	struct msess *s;

	s = malloc(sizeof (struct msess));
	memset(s, 0, sizeof (struct msess));

	LIST_INIT(&(s->probes));
	LIST_INSERT_HEAD(&sessions_head, s, entries);

	// add error check
	s->id = msess_next_id();

  return s;
	
}

/*
 * Find a msess entry for the given address and ID
 */
struct msess *msess_find(struct sockaddr *peer, msess_id id) {

	struct msess *sess;
	struct sockaddr_in6 *msess_addr, *query_addr;

	query_addr = (struct sockaddr_in6 *)peer;
	
	/* iterate all sessions and compare address */
	for (sess = sessions_head.lh_first; sess != NULL; sess = sess->entries.le_next) {

		msess_addr = (struct sockaddr_in6 *)&(sess->dst);

		if (memcmp(&(msess_addr->sin6_addr.s6_addr), &(query_addr->sin6_addr.s6_addr), sizeof query_addr->sin6_addr.s6_addr) == 0) {
			if (sess->id == id) {
				return sess;
			}
		}
	}

	// no msess found
	return NULL;

}

/*
 * Find next free id
 */
msess_id msess_next_id(void) {

	struct msess *sess;
	msess_id i;
	char free;

	for (i = 0; i < (msess_id)-1; i++) {

		free = 1;

		for (sess = sessions_head.lh_first; sess != NULL; sess = sess->entries.le_next) {

			if (sess->id == i) {
				free = 0;
			}
			//free |= (sess->id == i);
		}

		if (free) {
			return i;
		}
		
	}
	
	// all IDs in use
	return 0;

}

/*
 * Handle a new timestamp
 */
void mess_add_ts(struct sockaddr *peer, msess_id id, enum TS_TYPES tstype, struct timeval *ts) {

	struct msess *sess;

	/* find peer */
	sess = msess_find(peer, id); 
	
	/* if we receive a t1, we have a new session */
	if (tstype == T1) {

		struct msess_probe *p;
		p = malloc(sizeof (struct msess_probe));
		memset(p, 0, sizeof (struct msess_probe));
		memcpy(&(p->t1), ts, sizeof (struct timeval));

		LIST_INSERT_HEAD(&(sess->probes), p, entries);

	}

}

/*
 * Print current sessions to console
 */
void msess_printf(void) {

	int i = 0;
	char addr_str[INET6_ADDRSTRLEN];
	struct sockaddr_in6 *addr;
	struct msess *sess;

	for (sess = sessions_head.lh_first; sess != NULL; sess = sess->entries.le_next) {

		addr = (struct sockaddr_in6 *)&(sess->dst);
		inet_ntop(AF_INET6, addr->sin6_addr.s6_addr, addr_str, INET6_ADDRSTRLEN);
		printf("Measurement session %d:\n", i);
		printf(" Destination address: %s\n", addr_str);
		printf(" Destination port: %d\n", ntohs(addr->sin6_port));
		printf(" Interval: %d\n", sess->interval_usec);
		printf(" ID: %d\n", sess->id);
		printf("\n");
		i++;

	}

}
