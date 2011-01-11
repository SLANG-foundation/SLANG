#include "probed.h"
#include "msess.h"

int                s;       /* bind socket */
unsigned int       yes = 1; /* usefull macro */
unsigned int       no = 0; /* usefull macro */
struct sockaddr_in6 them;    /* other side's ip address */
int                slen;    /* size of sockaddr_in */

int main(int argc, char *argv[]) {

	int syslog_flags = 0;
	char *cfgfile = NULL;
	int arg;

	/* command line arguments */
	while ((arg = getopt(argc, argv, "vdc:")) != -1) {
		if (arg == 'v') syslog_flags |= LOG_PERROR;
		if (arg == 'd') debug(1);
		if (arg == 'c') {
			cfgfile = malloc(strlen(optarg));
			memcpy(cfgfile, optarg, strlen(optarg));
		}
	}

	/* syslog */
	openlog("probed", syslog_flags, LOG_USER);

	/* missing config file path in argc, using default */
	if (cfgfile == NULL) {
		config_init("settings.xml");
	} else {
		config_init(cfgfile);
	}

	/* initialize measurement session storage */
	msess_init();

	/* macros */
	slen = sizeof(them);

	/* read config, enabling timestamps */
	signal(SIGHUP, reload);
	reload();

	/* start the ping protocol loop */
	proto();

	/* close */
	close(s);
	closelog();
	return 0;

}

/*
 * Reload application
 */
void reload() {

	char tmp[TMPLEN];

	config_read();

	/* configure application */
	syslog(LOG_INFO, "Reloading configuration...");

	/* extra output */
	if (config_getkey("/config/debug", tmp, TMPLEN) == 0) {
		if (tmp[0] == 't' || tmp[0] == '1') debug(1); 
		 else debug(0);
	}

	/* server port */
	if (config_getkey("/config/port", tmp, TMPLEN) == 0)
		proto_bind(atoi(tmp));

	/* timestamping mode and interface, depend on socket/bind */
	config_getkey("/config/interface", c.iface, sizeof(c.iface)); 
	if (config_getkey("/config/timestamp", tmp, TMPLEN) < 0)
	       tstamp_hw(); /* default is hw with fallback */
	else {
		if (tmp[0] == 'k') tstamp_kernel();
		else if (tmp[0] == 's') tstamp_sw();
		else tstamp_hw();
	}

	/* (re)load measurement sessions */
	config_msess();

}
