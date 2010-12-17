#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/queue.h>
#include <arpa/inet.h>

#include "msess.h"

struct msess *sessions;
struct msess_head sessions_head;

/*
 * Initialize measurement session list
 */
void msess_init(void) {

	LIST_INIT(&sessions_head);

}

/*
 * Add measurement session to list
 */
void msess_add(struct msess *s) {

	LIST_INIT(&(s->probes));
	LIST_INSERT_HEAD(&sessions_head, s, entries);
	
}

/*
 * print current sessions to console
 */
void msess_printf(void) {

	int i = 0;
	char addr_str[INET_ADDRSTRLEN];
	struct sockaddr_in *addr;
	struct msess *sess;

	for (sess = sessions_head.lh_first; sess != NULL; sess = sess->entries.le_next) {

		addr = (struct sockaddr_in *)&(sess->dst);
		inet_ntop(AF_INET, &(addr->sin_addr.s_addr), addr_str, INET_ADDRSTRLEN);
		printf("Measurement session %d:\n", i);
		printf(" Destination address: %s\n", addr_str);
		printf(" Destination port: %d\n", ntohs(addr->sin_port));
		printf(" Interval: %d\n", sess->interval_usec);
		printf("\n");
		i++;

	}

}
