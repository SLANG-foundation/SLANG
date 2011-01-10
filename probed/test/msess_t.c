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

	unsigned int i, n, k, l, n_sess, n_probes;
	struct msess *a = 0;
	struct sockaddr_in6 addr;
	struct timeval t0, t1, t2, t3, t4, r;
	char addrstr[INET6_ADDRSTRLEN];

	struct sockaddr_in6 *hosts;
	msess_id *sess_id;

	struct s_probe {
		msess_id id;
		uint32_t seq;
		struct sockaddr_in6 *peer;
		enum TS_TYPES tstype;
		struct timeval ts;
	};

	struct s_probe *probes;
	struct s_probe tmp_probe;

	/* handle CLI arguments */
	if (argc < 2) {
		printf("%s num_sessions num_probes\n", argv[0]);
		exit(1);
	}
	n_sess = atoi(argv[1]);
	n_probes = atoi(argv[2]);
	
	printf("n_sess %d n_probes %d\n", n_sess, n_probes);

	hosts = malloc(sizeof (struct sockaddr_in6) * n_sess);
	sess_id = malloc(sizeof (msess_id) * n_sess);
	probes = malloc(sizeof (struct s_probe) * n_sess*n_probes*4);

	config_init();
	msess_init();

	/* create entries */
/*	printf("Generating %d destinations...\n", n_sess); */
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
		a = msess_add();
		a->interval_usec = 100000+100*i;
		a->dscp = i;
		a->last_seq = 0;
		sess_id[i] = a->id;
		memcpy(&a->dst, &hosts[i], sizeof addr);

	}

	/* msess_print_all(); */

	/*
	 * create probes
	 */

/*	printf("Generating probe data.\n"); */
	/* T1 */
	for (n = 0; n < n_probes; n++) {

		for (i = 0; i < n_sess; i++) {

			probes[n_sess*n+i].peer = &hosts[i];
			probes[n_sess*n+i].seq = n;
			probes[n_sess*n+i].tstype = T1;
			probes[n_sess*n+i].id = sess_id[i];

		}

	}

/*
	printf("In order:\n");
	for (i = 0; i < n_sess*n_probes; i+=1) {

		if (probes[i].peer == NULL) {
			printf("Missing peer for probe %d!\n", i);
		} else {
			inet_ntop(AF_INET6, probes[i].peer->sin6_addr.s6_addr, addrstr, INET6_ADDRSTRLEN);
			printf("Found addr %s and seq %d\n", addrstr, probes[i].seq);
		}
		
	} 
*/

	/* Copy T1 to T2-T4 */
	for (k = 1; k < 4; k++) {

		gettimeofday(&t0, NULL);

		for (i = n_sess*n_probes*k; i < n_sess*n_probes*(k+1); i++) {

			t0.tv_sec += random() % 6 - 3;
			memcpy(&probes[i], &probes[i-n_sess*n_probes*k], sizeof (struct s_probe));
			probes[i].tstype = (enum TS_TYPES) k;
			memcpy(&probes[i].ts, &t0, sizeof t0);

		}

	}

	/* Randomize order */
	for (k = 0; k < 4; k++) {

		for (i = n_sess*n_probes*k; i < n_sess*n_probes*(k+1); i++) {

			l = random() % n_sess*n_probes - i;

			memcpy(&tmp_probe, &probes[i], sizeof (struct s_probe));
			memcpy(&probes[i], &probes[i+l], sizeof (struct s_probe));
			memcpy(&probes[i+l], &tmp_probe, sizeof (struct s_probe));

		}

	}

	gettimeofday(&t1, NULL);
	diff_tv(&r, &t1, &t0);
	printf("generation %d values %d.%06d s\n", n_sess*n_probes*4, (int)r.tv_sec, (int)r.tv_usec);

/*
	printf("Randomized order:\n");
	for (i = 0; i < n_sess*n_probes*4; i++) {

		inet_ntop(AF_INET6, probes[i].peer->sin6_addr.s6_addr, addrstr, INET6_ADDRSTRLEN);
		printf("Found addr %10s and seq %5d\n", addrstr, probes[i].seq);
		
	} 
*/

	/* Add "received" timestamps; first create sessions (add T1) */
/*	printf("Creating sessions (adding T1).\n"); */
	for (i = 0; i < n_sess*n_probes; i++) {

		a = msess_find(probes[i].id);
		if (a == NULL) {
			inet_ntop(AF_INET6, probes[i].peer->sin6_addr.s6_addr, addrstr, INET6_ADDRSTRLEN);
			printf("Unable to find msess with addr %10s id %3d!\n", addrstr, probes[i].id);
			continue;
		}

		msess_add_ts(a, probes[i].seq, probes[i].tstype, &probes[i].ts);

	}

	gettimeofday(&t2, NULL);
	diff_tv(&r, &t2, &t1);
	printf("adding %d values %d.%06d s\n", n_sess*n_probes, (int)r.tv_sec, (int)r.tv_usec);

	/* add T2-T3 */
/*	printf("Saving T2-T4.\n"); */
	for (i = n_sess*n_probes; i < n_sess*n_probes*4; i++) {

		a = msess_find(probes[i].id);
		if (a == NULL) {
			inet_ntop(AF_INET6, probes[i].peer->sin6_addr.s6_addr, addrstr, INET6_ADDRSTRLEN);
			printf("Unable to find msess with addr %10s id %3d!\n", addrstr, probes[i].id);
			continue;
	}

		msess_add_ts(a, probes[i].seq, probes[i].tstype, &probes[i].ts);
	}

	gettimeofday(&t3, NULL);
	diff_tv(&r, &t3, &t2);
	printf("saving %d values %d.%06d s\n", n_sess*n_probes*3, (int)r.tv_sec, (int)r.tv_usec);
	msess_flush();
	gettimeofday(&t4, NULL);
	diff_tv(&r, &t4, &t3);
	printf("flushing %d probes, not all written to DB %d.%06d s\n", n_sess*n_probes, (int)r.tv_sec, (int)r.tv_usec);

	diff_tv(&r, &t4, &t1);
	printf("total %d.%0d s\n", (int)r.tv_sec, (int)r.tv_usec);
	
	return 0;

}
