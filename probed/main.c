/**
 * \file   main.c
 * \brief  main() that parses arguments and hands over to mainloop.c
 * \author Anders Berggren <anders@halon.se>
 * \author Lukas Garberg <lukas@spritelink.net>
 * \date   2010-11-01
 */ 

#ifndef S_SPLINT_S /* SPlint 3.1.2 bug */
#include <unistd.h>
#endif
#include <string.h>
#include <netdb.h>
#include "probed.h"
#include "msess.h"

struct config cfg;

/**
 * Sets default values, parses arguments, and start main loop. General
 * SLA-NG documentation is found for loop_or_die() in mainloop.c
 */
int main(int argc, char *argv[]) {

	int arg, s_udp, s_tcp, log, ret_val;
	char tstamp;
	char *addr, *iface, *port, *cfgpath, *wait;
	struct msess *client_msess;
	struct addrinfo /*@dependent@*/ dst_hints, *dst_addr;

	/* Default settings */
	cfgpath = "settings.xml";
	log = LOG_PERROR; /* Print to stdout */
	iface = "eth0"; /* Why not hehe */
	port = "60666"; /* Sexy port */
	cfg.op = 'h'; /* Operation mode: help */
	tstamp = 'h'; /* Timestamp mode: hardware */
	addr = "";
	wait = "500000";

	p(APP_AND_VERSION);
	debug(0);

	/* Command line arguments */
	/*@ -branchstate OK that opcode. etc changes storage @*/
	/*@ -unrecog OK that 'getopt' and 'optarg' is missing; SPlint bug */
	/* +charintliteral OK to compare 'arg' (int) int with char @*/
	while ((arg = getopt(argc, argv, "qdvshc:i:p:w:ku")) != -1) {
		if (arg == (int)'h') help_and_die();
		if (arg == (int)'?') exit(EXIT_FAILURE);
		if (arg == (int)'q') log = 0;
		if (arg == (int)'v') debug(1);
		if (arg == (int)'f') cfgpath = optarg;
		if (arg == (int)'i') iface = optarg;
		if (arg == (int)'p') port = optarg;
		if (arg == (int)'r') wait = optarg;
		if (arg == (int)'k') tstamp = 'k';
		if (arg == (int)'u') tstamp = 'u';
		if (arg == (int)'d') cfg.op = OPMODE_DAEMON;
		if (arg == (int)'s') cfg.op = OPMODE_SERVER;
		if (arg == (int)'c') {
			cfg.op = OPMODE_CLIENT;
			addr = optarg;
		}
	}
	if (cfg.op == 'h') help_and_die();
	/*@ +branchstate -charintliteral +unrecog @*/

	/* Startup config, logging and sockets */
	openlog("probed", log, LOG_USER);
	msess_init();
	bind_or_die(&s_udp, &s_tcp, (uint16_t)strtoul(port, NULL, 0));
	if (tstamp == 'h') tstamp_mode_hardware(s_udp, iface);
	if (tstamp == 'k') tstamp_mode_kernel(s_udp);
	if (tstamp == 'u') tstamp_mode_userland(s_udp);

	/* Start server, client or daemon */
	if (cfg.op == OPMODE_SERVER) loop_or_die(s_udp, s_tcp);
	if (cfg.op == OPMODE_CLIENT) {
		client_msess = msess_add(0);
		client_msess->interval.tv_sec = 0;
		client_msess->interval.tv_usec = atoi(wait);

		/* prepare for getaddrinfo */
		memset(&dst_hints, 0, sizeof dst_hints);
		dst_hints.ai_family = AF_INET6;
		dst_hints.ai_flags = AI_V4MAPPED;

		/* get address */
		ret_val = getaddrinfo(addr, port, &dst_hints, &dst_addr);
		if (ret_val < 0) {
			syslog(LOG_ERR, "Unable to look up hostname %s: %s", addr, 
					gai_strerror(ret_val));
			exit(EXIT_FAILURE);
		}
		memcpy(&client_msess->dst, dst_addr->ai_addr, sizeof client_msess->dst);
		loop_or_die(s_udp, s_tcp);
	}

	if (cfg.op == OPMODE_DAEMON) {
		p("Daemon mode; both server and client, output to pipe");
		/* read config */
		reload(cfgpath);
		(void)config_msess();
		loop_or_die(s_udp, s_tcp);
	}

	(void)close(s_udp);
	(void)close(s_tcp);
	closelog();
	exit(EXIT_FAILURE);
}

/**
 * Prints the CLI help message, when 'probed' is started without arguments
 */
void help_and_die(void) {
	p("usage: probed [-saqd] [-c addr] [-t type] [-i iface] [-p port]");
	p("");
	p("\t          MODES OF OPERATION");
	p("\t-c addr   Client mode: PING 'addr', fetch UDP timestamps");
	p("\t-s        Server mode: respond to PING, send UDP timestamps");
	p("\t-d        Daemon mode: both server and client, output to pipe");
	p("");
	p("\t          OPTIONS");
	p("\t-k        Create timestamps in kernel driver instead of hardware");
	p("\t-u        Create timestamps in userland instead of hardware");
	p("\t-i iface  Network interface used for hardware timestamping");
	p("\t-p port   UDP port, both source and destination");
	p("\t-w usecs  Client mode wait time between PINGs, in microseconds");
	p("\t-v        Output more debugging");
	p("\t-q        Be quiet, log error to syslog only");
	exit(EXIT_FAILURE);
}

/*
 * Reload application
 */
void reload(char *cfgpath) {

	char tmp[TMPLEN] = "";

	if (config_read(cfgpath) < 0) {
		syslog(LOG_ERR, "Invalid configuration, using default values");
	};
	/* configure application */
	syslog(LOG_INFO, "Reloading configuration...");
	/* extra output */
	if (config_getkey("/config/debug", tmp, TMPLEN) == 0) {
		if (tmp[0] == 't' || tmp[0] == '1') debug(1); 
		 else debug(0);
	}

}
