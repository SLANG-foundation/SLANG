#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <syslog.h>
#include <errno.h>

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
#include "msess.h"

#ifndef SO_TIMESTAMPING
#define SO_TIMESTAMPING 37
#define SCM_TIMESTAMPING SO_TIMESTAMPING
#endif
#define TYPE_PING 'i'
#define TYPE_PONG 'o'
#define TYPE_TIME 't'
#define USLEEP 1 /* the read timeout resolution, sets max pps */ 
#define TMPLEN 512
#define MSESS_NODE_NAME "probe"

extern char         *cfgpath;/* config file path */
extern xmlDoc* doc;
extern int                s;       /* bind socket */
extern unsigned int       yes;     /* usefull macro */
extern unsigned int       no;      /* usefull macro */
extern int                slen;    /* size of sockaddr_in */
extern struct sockaddr_in6 them;    /* other side's ip address */
extern struct cfg         c;
extern struct packet      p;

struct cfg {                       /* CONFIGURATION */
	char              debug;   /* extra output */
	int               port;    /* server port */
	char              ts;      /* timestamping mode (s)w (k)ern (h)w */
	char              iface[9];/* hw timestamping interface */
};
struct packet {                    /* PACKET */
	char              data[40];/* message data */
	struct timespec   ts;      /* latest fetched timestamp */
};
struct packet_ping {
	char type;
	int32_t seq;
	msess_id id;
	char data[32];
};
struct packet_time {
	char type;
	int32_t seq1;
	struct timespec rx1;
	struct timespec tx1;
	int32_t seq2;
	struct timespec rx2;
	struct timespec tx2;
	int32_t seq3;
	struct timespec rx3;
	struct timespec tx3;
	int32_t seq4;
	struct timespec rx4;
	struct timespec tx4;
	int32_t seq5;
	struct timespec rx5;
	struct timespec tx5;
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
enum TS_TYPES {
	T1,
	T2,
	T3,
	T4
};

int main(int argc, char *argv[]);
void die(char *msg);
void debug(char enabled);
void diff_ts (struct timespec *r, struct timespec *end, struct timespec *beg);
void diff_tv (struct timeval *r, struct timeval *end, struct timeval *beg);
int cmp_ts(struct timespec *t1, struct timespec *t2);
int cmp_tv(struct timeval *t1, struct timeval *t2);
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
void config_msess(void);
void reload();
