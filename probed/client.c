/*
 * UDP PING SOCKET CLIENT 
 * Author: Anders Berggren
 *
 */

#include "sla-ng.h"

void client() {
	struct timeval tv;
	
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
		perror("setsockopt: SO_RCVTIMEO");
	while (1) {
		them.sin_family = AF_INET;
		them.sin_port = htons(c.port);
		if (inet_aton("10.10.0.2", &them.sin_addr) < 0)
			perror("inet_aton: Check the IP address");
		client_fsm();
		usleep(100000);
	}
	close(s);
}

void client_fsm() {
	struct timespec rx, tx, di;
	char *data;
	int rtt, sdi;

	data_send("ANDERS\n", 7); /* send ping */
	tx = p.ts; /* save timestamp */

	data_recv(0); /* wait for pong */
	rx = p.ts; /* save timestamp */
	
	data_recv(0); /* wait for diff */
	
	diff_ts(&di, &rx, &tx);
	if (c.debug) printf("DI %010ld.%09ld\n", di.tv_sec, di.tv_nsec);
	data = p.data;
	data += 11;
	sdi = atoi(data);
	if (c.debug) printf("SD %d\n", sdi);
	rtt = di.tv_nsec - sdi;
	printf("RT %d\n", rtt);
}
