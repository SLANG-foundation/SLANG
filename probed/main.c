#include "probed.h"
#include "msess.h"

int                s;       /* bind socket */
unsigned int       yes = 1; /* usefull macro */
unsigned int       no = 0; /* usefull macro */
struct sockaddr_in6 them;    /* other side's ip address */
int                slen;    /* size of sockaddr_in */

int main(int argc, char *argv[]) {
	int log = LOG_PERROR;
	int arg;
	char mode = 'h', type = 'h';
	char *caddr, *iface = "eth0", *port = "60666";

	puts("SLA-NG probed 0.1");
	/* command line arguments */
	while ((arg = getopt(argc, argv, "qdashc:i:t:p:")) != -1) {
		if (arg == 'h') help();
		if (arg == '?') exit(EXIT_FAILURE);
		if (arg == 'q') log = 0;
		if (arg == 'd') debug(1);
		if (arg == 'a') mode = 'a';
		if (arg == 's') mode = 's';
		if (arg == 'c') {
			mode = 'c';
			caddr = optarg;
		}
	}
	openlog("probed", log, LOG_USER);
	/* start server, client or api */
	if (mode == 'h') {
		help();
	} else if (mode == 'a') {
			puts("API mode; both server and client, accepting IPC commands");
	} else if (mode == 'c') {
			printf("Client mode; Sending PING to %s:%s\n", caddr, port);
	} else if (mode == 's') {
			puts("Server mode; respondning to PING");
	}
	/* initialize measurement session storage */
	msess_init();
	/* macros */
	slen = sizeof(them);
	/* read config, enabling timestamps */
	signal(SIGHUP, config_read);
	config_read();
	/* start the ping protocol loop */
	proto();
	/* close */
	close(s);
	closelog();
	return 0;

}

void help() {
	puts("usage: probed [-saqd] [-c addr] [-t type] [-i iface] [-p port]");
	puts("");
	puts("\t          MODES OF OPERATION");
	puts("\t-s        Server mode: respond to PING, send UDP timestamps");
	puts("\t-c addr   Client mode: PING 'addr', fetch UDP timestamps");
	puts("\t-a        API mode: both server and client, accept IPC commands");
	puts("");
	puts("\t          OPTIONS");
	puts("\t-t type   Timestamp method; (h)ardware, (k)ernel, (u)serland");
	puts("\t-i iface  Network interface used for hardware timestamping");
	puts("\t-p port   UDP port, both source and destination");
	puts("\t-d        Output more debugging");
	puts("\t-q        Be quiet, log error to syslog only");
	exit(EXIT_FAILURE);
}
