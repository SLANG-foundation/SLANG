#include <sys/socket.h>
#include <netinet/in.h>
#include "msess.h"
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <syslog.h>

#ifndef SO_TIMESTAMPING
#define SO_TIMESTAMPING 37
#define SCM_TIMESTAMPING SO_TIMESTAMPING
#endif
#define TYPE_PING 'i'
#define TYPE_PONG 'o'
#define TYPE_TIME 't'
#define USLEEP 1 /* the read timeout resolution, sets max pps */ 
#define TMPLEN 512
#define DATALEN 40
#define MSESS_NODE_NAME "probe"

extern struct config cfg;

struct config {
	char debug; /* extra output */
	int port; /* server port */
	char ts; /* timestamping type (u)ser (k)ern (h)w */
};
struct packet {
	struct sockaddr_in6 addr; 
	char data[DATALEN];
	struct timespec ts;
};
struct packet_p {
	struct sockaddr_in6 *addr;
	char *data;
	struct timespec *ts;
};
struct packet_ping {
	char type;
	int32_t seq;
	msess_id id;
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
void help();
void reload(xmlDoc **cfgdoc, char *cfgpath);
void debug(char enabled);
void diff_ts (struct timespec *r, struct timespec *end, struct timespec *beg);
void diff_tv (struct timeval *r, struct timeval *end, struct timeval *beg);
int cmp_ts(struct timespec *t1, struct timespec *t2);
int cmp_tv(struct timeval *t1, struct timeval *t2);

int udp_bind(int *sock, int port);
int udp_client(int sock, char *addr, char *port);

void tstamp_mode_hardware(int sock, char *iface);
void tstamp_mode_kernel(int sock);
void tstamp_mode_userland(int sock);
int tstamp_extract(struct msghdr *msg, struct timespec *ts);
int tstamp_fetch_tx(int sock, struct timespec *ts);

int recv_w_ts(int sock, int flags, struct packet *pkt);
//int send_w_ts(int sock, char *d, int size, struct sockaddr_in6 *addr, struct timespec *ts_p);
int send_w_ts(int sock, struct packet_p *pkt_p);

int config_read(xmlDoc **doc, char *cfgpath);
int config_getkey(xmlDoc *doc, char *xpath, char *str, size_t bytes);
int config_msess(xmlDoc *doc);
