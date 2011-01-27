/**
 * Definitions for msess.c.
 *
 * \author Anders Berggren
 * \author Lukas Garberg
 * \file msess.h
 */

#ifndef MSESS_H
#define MSESS_H 1

#include <sys/queue.h>

/* define structs to hold first element of linked lists */
LIST_HEAD(msess_head, msess);

/**
 * Measurement session ID.
 */
typedef uint16_t msess_id;

/** 
 * Struct for storing configuration for one measurement session.
 */
struct msess {
	msess_id id; /**< Measurement session ID. */
	struct sockaddr_in6 dst; /**< Destination address and port. */
	struct timeval interval; /**< Probe interval */
	uint8_t dscp; /**< DiffServ Code Point value of measurement session */
	pid_t child_pid; /**< PID of child process maintaining the TCP connection. */
	uint32_t last_seq; /**< Last sequence number sent */
	struct timeval last_sent; /**< Time last probe was sent */
	LIST_ENTRY(msess) entries;
};

void msess_init(void);
void msess_print(struct msess *sess);
void msess_print_all(void);
struct msess *msess_add(msess_id id);
void msess_remove(struct msess *sess);
struct msess *msess_find(msess_id id);
uint32_t msess_get_seq(struct msess *sess);
struct msess *msess_next(void);
void msess_reset(void);
void msess_add_or_update(struct msess *nsess);

#endif
