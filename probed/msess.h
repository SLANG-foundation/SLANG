#ifndef _MSESS_H
#define _MSESS_H 1

#include <time.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <stdint.h>
/* #include "probed.h" */

/* define structs to hold first element of linked lists */
LIST_HEAD(msess_head, msess);

typedef uint16_t msess_id;

/* Struct for storing configuration for one measurement session */
struct msess {
	msess_id id;
	struct sockaddr_storage dst;
	struct timeval interval;
	uint8_t dscp;
	uint32_t last_seq;
	struct timeval last_sent;
	LIST_ENTRY(msess) entries;
};

void msess_init(void);
void msess_print(struct msess *sess);
void msess_print_all(void);
struct msess *msess_add(msess_id id);
void msess_remove(struct msess *sess);
struct msess *msess_find(uint16_t id);
uint32_t msess_get_seq(struct msess *sess);
struct msess *msess_next(void);
void msess_reset(void);

#endif
