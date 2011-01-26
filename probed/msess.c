#include <string.h>
#include <arpa/inet.h>

#include "probed.h"
#include "msess.h"

struct msess *sessions, *cur;
struct msess_head sessions_head;
struct timespec timeout;

/**
 * Initialize measurement session list handler
 */
void msess_init(void) {

	LIST_INIT(&sessions_head);

}

/**
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

/**
 * Add to list or update already existing measurement session 
 */
void msess_add_or_update(struct msess *nsess) {

	struct msess *ssess;

	ssess = msess_find(nsess->id);

	/* does the session exist? */
	if (ssess == NULL) {
		/* add new */
		LIST_INSERT_HEAD(&sessions_head, nsess, entries);
		msess_reset();
		return;
	}

	/* update the sessions */
	ssess->dscp = nsess->dscp;
	memcpy(&ssess->interval, &nsess->interval, sizeof ssess->interval);
	memcpy(&ssess->dst, &nsess->dst, sizeof nsess->dst);
	free(nsess);

	return;

}

/**
 * Remove measurement session from list
 */
void msess_remove(struct msess *sess) {

	/* remove msess */
	(void)msess_next();
	LIST_REMOVE(sess, entries);
	free(sess);

} 

/**
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

/**
 * Prints all sessions currently configured to console
 */
void msess_print_all(void) {

	struct msess *sess;

	for (sess = sessions_head.lh_first; sess != NULL; sess = sess->entries.le_next) {
		msess_print(sess);
	}

}

/**
 * Print one session to console
 */
void msess_print(struct msess *sess) {

	char addr_str[INET6_ADDRSTRLEN];
	struct sockaddr_in6 *addr;

	addr = &(sess->dst);
	inet_ntop(AF_INET6, addr->sin6_addr.s6_addr, addr_str, INET6_ADDRSTRLEN);
	printf("Measurement session ID %d:\n", sess->id);
	printf(" Destination address: %s\n", addr_str);
	printf(" Destination port: %d\n", ntohs(addr->sin6_port));
	printf(" Interval: %d.%06d s\n", (int)sess->interval.tv_sec, (int)sess->interval.tv_usec);
	printf(" Last sequence number: %d\n", sess->last_seq);

	printf("\n");

}

/**
 * Function to iterate over all msesses.
 */
struct msess *msess_next(void) {

	struct msess *ret;

	ret = cur;

	if (ret != NULL) {
		cur = ret->entries.le_next;
	} else {
		msess_reset();
	}

	return ret;

}

/**
 * Reset msess iterator variable.
 */
void msess_reset(void) {

	cur = sessions_head.lh_first;

}

/*
 * Get sequence number.
 * Increases sequence number and returns next value.
 * @param sess Pointer to msess structure.
 * @return Next sequence number.
 */
uint32_t msess_get_seq(struct msess *sess) {

	sess->last_seq++;
	return sess->last_seq;

}
