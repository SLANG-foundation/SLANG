#ifndef S_SPLINT_S /* SPlint 3.1.2 bug */
#include <unistd.h>
#endif
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>

#include "probed.h"
#include "msess.h"

struct config cfg;

int main(int argc, char *argv[]) {

	int arg, s_udp, s_tcp, log, ret_val;
	char opmode, tstamp;
	char *addr, *iface, *port, *cfgpath;
	xmlDoc *cfgdoc = 0;
	struct msess *client_msess;
	struct addrinfo /*@dependent@*/ dst_hints, *dst_addr;

	/* Default settings */
	cfgpath = "settings.xml";
	log = LOG_PERROR; /* Print to stdout */
	iface = "eth0"; /* Why not hehe */
	port = "60666"; /* Sexy port */
	opmode = 'h'; /* Operation mode: help */
	tstamp = 'h'; /* Timestamp mode: hardware */
	addr = "";

	p(APP_AND_VERSION);
	debug(0);

	/* Command line arguments */
	/*@ -branchstate OK that opcode. etc changes storage @*/
	/*@ -unrecog OK that 'getopt' and 'optarg' is missing; SPlint bug */
	/* +charintliteral OK to compare 'arg' (int) int with char @*/
	while ((arg = getopt(argc, argv, "qdashc:i:p:ku")) != -1) {
		if (arg == (int)'h') help_and_die();
		if (arg == (int)'?') exit(EXIT_FAILURE);
		if (arg == (int)'q') log = 0;
		if (arg == (int)'d') debug(1);
		if (arg == (int)'f') cfgpath = optarg;
		if (arg == (int)'i') iface = optarg;
		if (arg == (int)'k') tstamp = 'k';
		if (arg == (int)'u') tstamp = 'u';
		if (arg == (int)'a') opmode = OPMODE_DAEMON;
		if (arg == (int)'s') opmode = OPMODE_SERVER;
		if (arg == (int)'c') {
			opmode = OPMODE_CLIENT;
			addr = optarg;
		}
	}
	if (opmode == 'h') help_and_die();
	/*@ +branchstate -charintliteral +unrecog @*/

	/* Startup config, logging and sockets */
	openlog("probed", log, LOG_USER);
	msess_init();
	bind_or_die(&s_udp, &s_tcp, (uint16_t)strtoul(port, NULL, 0));
	if (tstamp == 'h') tstamp_mode_hardware(s_udp, iface);
	if (tstamp == 'k') tstamp_mode_kernel(s_udp);
	if (tstamp == 'u') tstamp_mode_userland(s_udp);

	/* Start server, client or api */
	if (opmode == OPMODE_SERVER) loop_or_die(s_udp, s_tcp, NULL, port);
	if (opmode == OPMODE_CLIENT) {

		client_msess = msess_add(0);
		client_msess->interval.tv_sec = 0;
		client_msess->interval.tv_usec = 500000;

		/* prepare for getaddrinfo */
		memset(&dst_hints, 0, sizeof dst_hints);
		dst_hints.ai_family = AF_INET6;
		dst_hints.ai_flags = AI_V4MAPPED;

		/* get address */
		ret_val = getaddrinfo(addr, port, &dst_hints, &dst_addr);
		if (ret_val < 0) {
			syslog(LOG_ERR, "Unable to look up hostname %s: %s", addr, gai_strerror(ret_val));
		}
		memcpy(&client_msess->dst, dst_addr->ai_addr, sizeof client_msess->dst);

		loop_or_die(s_udp, s_tcp, addr, port);

	}

	if (opmode == OPMODE_DAEMON) {

		p("API mode; both server and client, accepting IPC commands");

		/* read config */
		reload(&cfgdoc, cfgpath);
		config_msess(cfgdoc);
		msess_print_all();
		loop_or_die(s_udp, s_tcp, "::1", port);

	}

	(void)close(s_udp);
	(void)close(s_tcp);
	closelog();
	return EXIT_FAILURE;
}

void help_and_die(void) {
	p("usage: probed [-saqd] [-c addr] [-t type] [-i iface] [-p port]");
	p("");
	p("\t          MODES OF OPERATION");
	p("\t-s        Server mode: respond to PING, send UDP timestamps");
	p("\t-c addr   Client mode: PING 'addr', fetch UDP timestamps");
	p("\t-a        API mode: both server and client, accept IPC commands");
	p("");
	p("\t          OPTIONS");
	p("\t-k        Create timestamps in kernel driver instead of hardware");
	p("\t-u        Create timestamps in userland instead of hardware");
	p("\t-i iface  Network interface used for hardware timestamping");
	p("\t-p port   UDP port, both source and destination");
	p("\t-d        Output more debugging");
	p("\t-q        Be quiet, log error to syslog only");
	exit(EXIT_FAILURE);
}
/*
 * Reload application
 */
void reload(xmlDoc **cfgdoc, char *cfgpath) {

	char tmp[TMPLEN] = "";

	if (config_read(cfgdoc, cfgpath) < 0) {
		syslog(LOG_ERR, "Invalid configuration, using default values");
	};
	/* configure application */
	syslog(LOG_INFO, "Reloading configuration...");
	/* extra output */
	if (config_getkey(*cfgdoc, "/config/debug", tmp, TMPLEN) == 0) {
		if (tmp[0] == 't' || tmp[0] == '1') debug(1); 
		 else debug(0);
	}

}

