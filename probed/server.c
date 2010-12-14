/*
 * UDP PING SOCKET SERVER
 * Author: Anders Berggren
 *
 * Very crappy starting point for a HW UDP ping (SLA-NG)
 * server...
 */

#include "sla-ng.h"

void server() {
	struct sockaddr_in addr;

	/* bind to port */
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(c.port);
	if (bind(s, (struct sockaddr *)&addr, slen) < 0) perror("bind");
	/* state machine */
	server_fsm();
	close(s);
}

void server_fsm() {
	struct timespec rx, tx, di;
	char d[512];
	
	while (1) {
		data_recv(0); /* wait for ping */
		rx = p.ts; /* save timestamp */
		
		data_send("ANDERS\n", 7); /* send pong */
		tx = p.ts; /* save timestamp */

		diff_ts(&di, &tx, &rx);
		if (c.debug) printf("DI %010ld.%09ld\n", di.tv_sec, di.tv_nsec);
		
		snprintf(d, 512, "%010ld.%09ld\n", di.tv_sec, di.tv_nsec);
		data_send(d, strlen(d)); /* send diff */
	}
}
