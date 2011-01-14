#include "probed.h"
#include <unistd.h>

struct config cfg;

int main(int argc, char *argv[]) {
	int arg, sock, log;
	char opmode, tstamp;
	char *caddr, *iface, *port, *cfgpath;
	xmlDoc *cfgdoc = 0;

	/* default settings */
	log = LOG_PERROR;
	cfgpath = "settings.xml";
	iface = "eth0";
	port = "60666";
	opmode = 'h'; /* operation mode: help */
	tstamp = 'h'; /* timestamp mode: hardware */

	puts("SLA-NG probed 0.1");
	setlogmask(LOG_UPTO(LOG_INFO)); /* default syslog level */
	/* command line arguments */
	while ((arg = getopt(argc, argv, "qdashc:i:p:ku")) != -1) {
		if (arg == 'h') help();
		if (arg == '?') exit(EXIT_FAILURE);
		if (arg == 'q') log = 0;
		if (arg == 'd') debug(1);
		if (arg == 'f') cfgpath = optarg;
		if (arg == 'i') iface = optarg;
		if (arg == 'k') tstamp = 'k';
		if (arg == 'u') tstamp = 'u';
		if (arg == 'a') opmode = 'a';
		if (arg == 's') opmode = 's';
		if (arg == 'c') {
			opmode = 'c';
			caddr = optarg;
		}
	}
	if (opmode == 'h') help();
	/* startup config, logging and sockets */
	openlog("probed", log, LOG_USER);
	reload(&cfgdoc, cfgpath);
	udp_bind(&sock, atoi(port));
	if (tstamp == 'h') tstamp_mode_hardware(sock, iface);
	if (tstamp == 'u') tstamp_mode_userland(sock);
	if (tstamp == 'k') tstamp_mode_kernel(sock);
	/* start server, client or api */
	if (opmode == 's') puts("Server mode; respondning to PING");
	if (opmode == 'c') udp_client(sock, caddr, port);
	if (opmode == 'a') puts("API mode; both server and client, accepting IPC commands");

	closelog();
	return EXIT_FAILURE;
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
	puts("\t-k        Create timestamps in kernel driver instead of hardware");
	puts("\t-u        Create timestamps in userland instead of hardware");
	puts("\t-i iface  Network interface used for hardware timestamping");
	puts("\t-p port   UDP port, both source and destination");
	puts("\t-d        Output more debugging");
	puts("\t-q        Be quiet, log error to syslog only");
	exit(EXIT_FAILURE);
}
/*
 * Reload application
 */
void reload(xmlDoc **cfgdoc, char *cfgpath) {
	char tmp[TMPLEN];

	config_read(cfgdoc, cfgpath);
	/* configure application */
	syslog(LOG_INFO, "Reloading configuration...");
	/* extra output */
	if (config_getkey(*cfgdoc, "/config/debug", tmp, TMPLEN) == 0) {
		if (tmp[0] == 't' || tmp[0] == '1') debug(1); 
		 else debug(0);
	}
	/* server port */
	/*if (config_getkey(*cfgdoc, "/config/port", tmp, TMPLEN) == 0)
		proto_bind(atoi(tmp));*/
	/* (re)load measurement sessions */
	/*config_msess(*cfgdoc);*/
}

void debug(char enabled) {
	cfg.debug = enabled;
	if (enabled) setlogmask(LOG_UPTO(LOG_DEBUG));
	else setlogmask(LOG_UPTO(LOG_INFO));
	return;
}
