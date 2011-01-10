#include "probed.h"
#include "msess.h"

int                s;       /* bind socket */
unsigned int       yes = 1; /* usefull macro */
unsigned int       no = 0; /* usefull macro */
struct sockaddr_in6 them;    /* other side's ip address */
int                slen;    /* size of sockaddr_in */

int main(int argc, char *argv[]) {
	int syslog_flags = 0;
	int arg;

	/* command line arguments */
	while ((arg = getopt(argc, argv, "vd")) != -1) {
		if (arg == 'v') syslog_flags |= LOG_PERROR;
		if (arg == 'd') debug(1);
	}

	/* syslog */
	openlog("probed", syslog_flags, LOG_USER);

	/* get initial xml config to *doc */
	config_init();

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

