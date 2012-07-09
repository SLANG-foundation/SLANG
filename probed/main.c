/**
 * \file   main.c
 * \brief  main() that parses arguments and hands over to mainloop.c
 * \author Anders Berggren <anders@halon.se>
 * \author Lukas Garberg <lukas@spritelink.net>
 * \date   2010-11-01
 */

#include <stdlib.h>
#ifndef S_SPLINT_S /* SPlint 3.1.2 bug */
#include <unistd.h>
#endif
#include <signal.h>
#include <string.h>
#include <netdb.h>
#include <syslog.h>
#include "probed.h"
#include "util.h"
#include "tstamp.h"
#include "client.h"
#include "loop.h"
#include "net.h"

struct config cfg;
int main(int argc, char *argv[]);
static void help_and_die(void);
static void reload(/*@unused@*/ int sig);

/**
 * Sets default values, parses arguments, and start main loop. General
 * SLA-NG documentation is found for loop_or_die() in loop.c
 */
int main(int argc, char *argv[]) {
	int arg, s_udp, s_tcp, log;
	enum tsmode tstamp;
	char *addr, *iface, *port, *cfgpath, *fifopath, *wait;

	/* Default settings */
	cfgpath = "probed.conf";
	log = LOG_PERROR; /* Print to stdout */
	iface = "eth0"; /* Why not, Linux standard */
	port = "60666"; /* Sexy port */
	cfg.op = HELP; /* Operation mode */
	tstamp = HARDWARE; /* Timestamp mode */
	addr = "";
	fifopath = "";
	wait = "500";
	count_server_resp = 0;
	count_client_sent = 0;
	count_client_done = 0;
	count_client_find = 0;
	count_client_fifoq = 0;
	count_client_fifoq_max = 0;

	p(APP_AND_VERSION);
	debug(0);

	/* Command line arguments */
	/*@ -branchstate OK that opcode. etc changes storage @*/
	/*@ -unrecog OK that 'getopt' and 'optarg' is missing; SPlint bug */
	/* +charintliteral OK to compare 'arg' (int) int with char @*/
	while ((arg = getopt(argc, argv, "hqf:i:p:w:kusc:d:")) != -1) {
		if (arg == (int)'h') help_and_die();
		if (arg == (int)'?') exit(EXIT_FAILURE);
		if (arg == (int)'q') log = 0;
		if (arg == (int)'f') cfgpath = optarg;
		if (arg == (int)'i') iface = optarg;
		if (arg == (int)'p') port = optarg;
		if (arg == (int)'w') wait = optarg;
		if (arg == (int)'k') tstamp = KERNEL;
		if (arg == (int)'u') tstamp = USERLAND;
		if (arg == (int)'s') cfg.op = SERVER;
		if (arg == (int)'d') {
			cfg.op = DAEMON;
			fifopath = optarg;
		}
		if (arg == (int)'c') {
			cfg.op = CLIENT;
			addr = optarg;
		}
	}
	if (cfg.op == HELP) help_and_die();
	/*@ +branchstate -charintliteral +unrecog @*/

	/* Startup config, logging and sockets */
	openlog("probed", log, LOG_USER);
	bind_or_die(&s_udp, &s_tcp, port);
	if (tstamp == HARDWARE) tstamp_mode_hardware(s_udp, iface);
	if (tstamp == KERNEL) tstamp_mode_kernel(s_udp);
	if (tstamp == USERLAND) tstamp_mode_userland(s_udp);

	/* Start server, client or daemon */
	if (cfg.op == SERVER) {
		syslog(LOG_INFO, "Server mode: waiting for PINGs\n");
		/* Launch */
		loop_or_die(s_udp, s_tcp, port, cfgpath);
	} else if (cfg.op == CLIENT) {
		/* Create PING results array */
		client_init();
		/* Add one measurement session */
		if (client_msess_add(port, addr, 0, atoi(wait), 0) != 0)
			exit(EXIT_FAILURE);
		/* When loop_or_die starts, reload config (fork!) immediatelly */
		cfg.should_reload = 1;
		/* Print results on Ctrl+C */
		(void)signal(SIGINT, client_res_summary);
		/* Launch */
		loop_or_die(s_udp, s_tcp, port, cfgpath);
	} else { /* Implicit cfg.op == DAEMON */
		p("Daemon mode; both server and client, output to pipe");
		/* Create PING results array and FIFO */
		client_init();
		client_res_fifo_or_die(fifopath);
		/* Reload configuration on HUP */
		(void)signal(SIGHUP, reload);
		(void)signal(SIGALRM, reload);
		/* When loop_or_die starts, reload config immediatelly */
		cfg.should_reload = 1;
		/* Launch */
		loop_or_die(s_udp, s_tcp, port, cfgpath);
	}
	/* We will never get here */
	(void)close(s_udp);
	(void)close(s_tcp);
	closelog();
	exit(EXIT_FAILURE);
}

/**
 * Prints the CLI help message, when 'probed' is started without arguments
 */
static void help_and_die(void) {
	p("usage: probed [-kqsu] [-c addr] [-d path] [-i iface] [-p port] [-f path]");
	p("");
	p("\t          MODES OF OPERATION");
	p("\t-c addr   Client: PING 'addr', print to standard output");
	p("\t-s        Server: respond to PINGs");
	p("\t-d path   Daemon: server and (many) clients, print to FIFO 'path'");
	p("");
	p("\t          OPTIONS");
	p("\t-f path   Daemon only, path to config file [default: probed.conf]");
	p("\t-w time   Client only, wait time between PINGs [default 500] (ms)");
	p("\t-i iface  Network interface for hardware timestamps [default: eth0]");
	p("\t-p port   UDP port, both source and destination [default: 60666]");
	p("\t-k        Create timestamps in kernel driver instead of hardware");
	p("\t-u        Create timestamps in userland instead of hardware");
	p("\t-q        Be quiet, log to syslog only");
	exit(EXIT_FAILURE);
}

/*
 * Reload application
 */
static void reload(int sig) {
	cfg.should_reload = 1;
}
