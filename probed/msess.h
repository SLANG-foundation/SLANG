//#include <time.h>
#include <sys/socket.h>
#include <sys/queue.h>
#include <stdint.h>

/* define structs to hold first element of linked lists */
LIST_HEAD(msess_head, msess);
LIST_HEAD(probes_head, msess_probe);

typedef uint16_t msess_id;

/* Struct for storing configuration for one measurement session */
struct msess {
	msess_id id;
	struct sockaddr_storage dst;
	uint32_t interval_usec;
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
	struct timeval t1;
	struct timeval t2;
	struct timeval t3;
	struct timeval t4;
	LIST_ENTRY(msess_probe) entries;
};

void msess_init(void);
void msess_printf(void);
msess_id msess_next_id(void);
struct msess *msess_add(void);
struct msess *msess_find(struct sockaddr *peer, uint16_t id);
