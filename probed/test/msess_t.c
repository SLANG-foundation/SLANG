#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "probed.h"
#include "msess.h"
/* #include "test.h" */

/*
 * Create n_sess sessions.
 * Pre-generate n_probes probes per session.
 * Populate session with n_probes each.
 * Update timestamps of all probes in all sessions.
 * 
 * For each pre-generated probe we need the following:
 *  - host (pointer to sockaddr_in6)
 *  - sequence number (in random order)
 *  - timestamp
 *  - timestamp type (in increasing order)
 *  - ID of measurement
 *
 */

int main(int argc, char *argv[]) {

	unsigned int i, n_sess;
	struct msess *a = 0;
	struct sockaddr_in6 addr;
	struct timeval t0, t1, t4, r;
	char addrstr[INET6_ADDRSTRLEN];

	struct sockaddr_in6 *hosts;
	msess_id *sess_id;

	/* handle CLI arguments */
	if (argc < 1) {
		printf("%s num_sessions\n", argv[0]);
		exit(1);
	}

	n_sess = atoi(argv[1]);
	
	printf("n_sess %d\n", n_sess);

	hosts = malloc(sizeof (struct sockaddr_in6) * n_sess);
	sess_id = malloc(sizeof (msess_id) * n_sess);

	openlog("msess_t", LOG_PERROR, LOG_USER);
	config_init();
	msess_init();

	/* create entries */
	printf("Generating %d destinations...\n", n_sess);
	gettimeofday(&t0, NULL);
	for (i = 0; i < n_sess; i++) {

		/* create host (sockaddr_in6) */
		memset(&hosts[i], 0, sizeof hosts[i]);
		hosts[i].sin6_family = AF_INET6;
		hosts[i].sin6_port = htons(60666);
		sprintf(addrstr, "1::%X:1", i);
/*		printf("Generated address %s\n", addrstr); */
		inet_pton(AF_INET6, addrstr, &(hosts[i].sin6_addr.s6_addr));

		// create session
		a = msess_add((msess_id)i);
		a->interval.tv_sec = 0;
		a->interval.tv_usec = 100000+100*i;
		a->dscp = i;
		a->last_seq = 0;
		sess_id[i] = a->id;
		memcpy(&a->dst, &hosts[i], sizeof addr);

	}

	msess_print_all();

	diff_tv(&r, &t4, &t1);
	printf("total %d.%0d s\n", (int)r.tv_sec, (int)r.tv_usec);
	
	return 0;

}
