#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>
#include <ctype.h>

#include <sys/time.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h> 
#include <sys/ioctl.h>

#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <net/if.h>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

#include "asm/types.h"
#include "linux/errqueue.h"
#include "net_tstamp.h"
#include "sockios.h"
//#include "linux/net_tstamp.h"
//#include "linux/sockios.h"

#ifndef SO_TIMESTAMPING
#define SO_TIMESTAMPING 37
#define SCM_TIMESTAMPING SO_TIMESTAMPING
#endif
#define TYPE_PING 'i'
#define TYPE_PONG 'o'
#define TYPE_TIME 't'
#define USLEEP 1 /* the read timeout resolution, sets max pps */ 
#define TMPLEN 512

/*extern const char *cfgpath;
extern xmlDoc* doc;*/
extern struct sockaddr_in6 them;    /* other side's ip address */
extern struct config cfg;
extern struct packet pkt;

struct config {
	char debug; /* extra output */
	int port; /* server port */
	char ts; /* timestamping type (u)ser (k)ern (h)w */
};
struct packet {
	struct sockaddr_in6 addr; 
	char data[40];
	struct timespec ts;
};
struct packet_ping {
	char type;
	int32_t seq;
	char data[32];
};
struct packet_time {
	char type;
	int32_t seq;
	struct timespec rx;
	struct timespec tx;
};
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
struct scm_timestamping {
	struct timespec systime;
	struct timespec hwtimesys;
	struct timespec hwtimeraw;
};
/*enum TS_TYPES {
	T1,
	T2,
	T3,
	T4
};*/

int main(int argc, char *argv[]);
void die(char *msg);
void debug(char enabled);
void diff_ts (struct timespec *r, struct timespec *end, struct timespec *beg);
void diff_tv (struct timeval *r, struct timeval *end, struct timeval *beg);
int cmp_ts(struct timespec *t1, struct timespec *t2);
void proto();
void proto_client();
void proto_server();
void proto_timestamp();
void proto_bind(int port);
void tstamp_hw();
void tstamp_kernel();
void tstamp_sw();
int tstamp_get(struct msghdr *msg);
void tstamp_recv();
int data_recv(int flags);
void data_send(char *data, int size);
void config_read();
void config_scan();
int config_getkey(char *xpath, char *str, size_t bytes);
void config_init();
