/* 
 * Copyright (c) 2011 Anders Berggren, Lukas Garberg, Tele2
 *
 * We have not yet decided upon a license, and so far it may only be
 * used and redistributed with our explicit permission.
 */ 

#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>

#define APP_AND_VERSION "SLA-NG probed 0.3"
/* Measurement time out [seconds] */
#define TIMEOUT 10
/* Tick interval [microseconds] */
#define SEND_INTERVAL 1000
/* Interval between aclculation of tick deviation [ticks] */
#define SEND_INTERVAL_DEV_INTERVAL 100
/* Interval between flush of timed out probes [nanoseconds] */
#define TIMEOUT_INTERVAL 100000
#define TMPLEN 512
#define DATALEN 48
/* Measurement status types */
#define TYPE_PING 1
#define TYPE_PONG 2
#define TYPE_TIME 3
#define TYPE_HELO 4
#define TYPE_SEND 5

int count_server_resp;
int count_client_sent;
int count_client_done;
int count_client_find;
int count_client_fifoq;
int count_client_fifoq_max;
int last_tx_id;
int last_tx_seq;

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
	volatile sig_atomic_t should_reload;
	volatile sig_atomic_t should_clear_timeouts;
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
	num_t type;
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
