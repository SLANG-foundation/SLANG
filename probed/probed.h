/* 
 * Copyright (c) 2011 Anders Berggren, Lukas Garberg, Tele2
 *
 * We have not yet decided upon a license, and so far it may only be
 * used and redistributed with our explicit permission.
 */ 

#include <sys/socket.h>
#include <netinet/in.h>

#define APP_AND_VERSION "SLA-NG probed 0.1"
#define TIMEOUT 10
#define TMPLEN 512
#define DATALEN 48
#define TYPE_PING 'i'
#define TYPE_PONG 'o'
#define TYPE_TIME 't'
#define TYPE_HELO 'h'
#define TYPE_SEND 's'

typedef struct timespec ts_t;
typedef struct sockaddr_in6 addr_t;
typedef uint32_t num_t;

enum opmode {
	HELP,
	SERVER,
	CLIENT,
	DAEMON
};
enum tsmode {
	HARDWARE,
	KERNEL,
	USERLAND
};
struct config {
	enum tsmode ts; /* timestamping type */
	enum opmode op; /* operation mode */
	int fifo; /* file descriptor to named pipe for daemon mode */
	int should_reload;
	int should_clear_timeouts;
};
extern struct config cfg;

struct packet {
	addr_t addr; 
	uint8_t dscp;
	char data[DATALEN];
	/*@dependent@*/ ts_t ts;
};
typedef struct packet pkt_t;
struct packet_data {
	char type;
	num_t seq;
	num_t id;
	/*@dependent@*/ ts_t t2;
	/*@dependent@*/ ts_t t3;
};
typedef struct packet_data data_t;

struct packet_rpm {
	int32_t t1_sec;
	int32_t t1_usec;
	int32_t t4_sec;
	int32_t t4_usec;
	int16_t version;
	int16_t magic;
	int32_t reserved;
	int32_t t2_sec;
	int32_t t2_usec;
	int32_t t3_sec;
	int32_t t3_usec;
};
