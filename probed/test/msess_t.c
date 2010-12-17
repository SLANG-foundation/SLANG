#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
/* #include <time.h> */

#include "msess.h"

int main(int argc, char *argv[]) {

	int i;
	struct msess *a, *b;
	struct sockaddr_in6 addr, *addr2;
	struct timeval t;
	char addrstr[INET6_ADDRSTRLEN];

	msess_init();


	// create entry
	for (i = 0; i < 10; i++) {

		a = msess_add();

		a->interval_usec = 100000+100*i;
		a->dscp = i;
		a->last_seq = 10202+i;

		sprintf(addrstr, "::ffff:127.0.0.%d", i);
		memset(&addr, 0, sizeof addr);
		addr.sin6_family = AF_INET6;
		addr.sin6_port = htons(60666);
		inet_pton(AF_INET6, addrstr, &(addr.sin6_addr.s6_addr));
		memcpy(&a->dst, &addr, sizeof addr);

	}

	msess_print_all();

	inet_pton(AF_INET6, "::ffff:127.0.0.5", &(addr.sin6_addr.s6_addr));

	printf("\nSEARCHING FOR MSESS\n");
	if ( ( a = msess_find((struct sockaddr *)&addr, 6) ) != NULL ) { 

		// print stuff
		addr2 = (struct sockaddr_in6 *)&(a->dst);
		inet_ntop(AF_INET6, addr2->sin6_addr.s6_addr, addrstr, INET6_ADDRSTRLEN);
		printf("Found addr %s :: port %d :: interval %d :: dscp %d :: id %d\n", addrstr, ntohs(addr2->sin6_port), a->interval_usec, a->dscp, a->id);

	} else {
		printf("msess not found.\n");
	}

	printf("Filling up sessions with data...\n");

	for (i = 0; i < 1000; i++) {

		sprintf(addrstr, "::ffff:127.0.0.%d", i % 10);
		inet_pton(AF_INET6, addrstr, &(addr.sin6_addr.s6_addr));
		memcpy(&(a->dst), &addr, sizeof addr);
		b = msess_find((struct sockaddr *)&addr, (i % 10) + 1);

		t.tv_sec = 1292600146+i;
		t.tv_usec = 100*(i % 10000);
		msess_add_ts(b, i, T1, &t);

		t.tv_sec = 1292600146+i;
		t.tv_usec = 100*2*(i % 10000);
		msess_add_ts(b, i, T2, &t);

		t.tv_sec = 1292600146+i;
		t.tv_usec = 100*3*(i % 10000);
		msess_add_ts(b, i, T3, &t);

		t.tv_sec = 1292600146+i;
		t.tv_usec = 100*4*(i % 10000);
		msess_add_ts(b, i, T4, &t);
		
	}

	inet_pton(AF_INET6, "::ffff:127.0.0.5", &(addr.sin6_addr.s6_addr));
	memcpy(&(a->dst), &addr, sizeof addr);

	b = msess_find((struct sockaddr *)&addr, 6);
	msess_print(b);

	return 0;

}
