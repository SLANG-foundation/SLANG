/**
 * Keeps track of measurement sessions.
 *
 * Maintains a list of current measurement sessions, each represented
 * by an instance of the struct msess.
 *
 * \file	msess.c
 * \author	Anders Berggren <anders@halon.se>
 * \author	Lukas Garberg <lukas@spritelink.net>
 */
#include <string.h>
#include <arpa/inet.h>

#include "probed.h"
#include "msess.h"

struct msess *cur; /**< Used for iterating the sessions. */
struct msess_head sessions_head; /**< Keeps a reference to the beginning of the list */

/**
 * Initialize measurement session list.
 */
void msess_init(void) {

	LIST_INIT(&sessions_head);

}

/**
 * Add measurement session to list.
 *
 * \param[in] id ID of the measurement session.
 * \return Pointer to the created measurement session.
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
 * Add new or update already existing measurement session.
 *
 * A function to help keeping the measurement session list in sync with
 * the configuration. If a measurement session with the same ID as the 
 * one we are trying to add already exists, modify it make it equal to
 * the one supplied. If no session is found, the supplied one is added 
 * to the list.
 * \n\n
 * If a copy was found, the supplied msess is free()-ed.
 *
 * \param[in] nsess Pointer to the msess to add.
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
 * Remove measurement session from list.
 *
 * Removes a given measurement session from the list and frees its 
 * memory.
 *
 * \param[in] sess Pointer to msess to remove.
 */
void msess_remove(struct msess *sess) {

	/* remove msess */
	(void)msess_next();
	LIST_REMOVE(sess, entries);
	free(sess);

} 

/**
 * Find msess with a specific ID.
 *
 * Finds the measurement session with the specified ID. If no matching
 * session is found, NULL is returned.
 *
 * \param[in] id ID of the measurement session.
 * \return Pointer to matching measurement session. If none found, NULL is returned.
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
 * Prints all sessions currently configured to console.
 */
void msess_print_all(void) {

	struct msess *sess;

	for (sess = sessions_head.lh_first; sess != NULL; sess = sess->entries.le_next) {
		msess_print(sess);
	}

}

/**
 * Print one session to console.
 *
 * \param[in] sess Pointer to measurement session to print.
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
 * Get next msess.
 *
 * A function to iterate the measurement session list.
 * For each time run, the function returns the next session in the list
 * until it reaches the end, when NULL is returned.\n\n
 * Example usage:
 * <tt> while ( (sess = msess_next()) ) {
 *   do something..
 * } </tt> \n
 * To start over, run msess_reset() (automatically run when the end of
 * the list is reached).
 *
 * \return Pointer to next msess.
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
 *
 * Used to start over the iteration of measurement sessions made by
 * msess_next().
 */
void msess_reset(void) {

	cur = sessions_head.lh_first;

}

/**
 * Get sequence number.
 *
 * Increases sequence number and returns next value.
 * \param[in] sess Pointer to msess structure.
 * \return Next sequence number.
 */
uint32_t msess_get_seq(struct msess *sess) {

	sess->last_seq++;
	return sess->last_seq;

}
