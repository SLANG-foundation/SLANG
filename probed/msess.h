#include <time.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <stdint.h>
/* #include "probed.h" */

/* constants */
#define PROBE_STATE_SUCCESS 0
#define PROBE_STATE_TIMEOUT 1

/* define structs to hold first element of linked lists */
LIST_HEAD(msess_head, msess);
LIST_HEAD(probes_head, msess_probe);

typedef uint16_t msess_id;

/* Struct for storing configuration for one measurement session */
struct msess {
	msess_id id;
	struct sockaddr_storage dst;
	uint32_t interval_usec;
	struct timespec timeout;
	uint8_t dscp;
	uint32_t last_seq;
	struct probes_head probes;
	LIST_ENTRY(msess) entries;
};

/*
 * Keeps state of sessions
 */
struct msess_probe {
	uint32_t seq;
	struct timespec t1;
	struct timespec t2;
	struct timespec t3;
	struct timespec t4;
	LIST_ENTRY(msess_probe) entries;
};

void msess_init(void);
void msess_print(struct msess *sess);
void msess_print_all(void);
void msess_add_ts(struct msess *sess, uint32_t seq, enum TS_TYPES tstype, struct timeval *ts);
msess_id msess_next_id(void);
struct msess *msess_add(void);
void msess_remove(struct msess *sess);
struct msess *msess_find(struct sockaddr *peer, uint16_t id);
void msess_probe_remove(struct msess_probe *p);
