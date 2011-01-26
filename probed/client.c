#include <string.h>
#include <time.h>
#include <sys/queue.h>
#include "probed.h"

/*@ -exportlocal TODO wtf */
int res_response = 0;
int res_timeout = 0;
long long res_rtt_total = 0;
ts_t res_rtt_min, res_rtt_max;
/*@ +exportlocal */

void client_res_init(void) {
	/*@ -mustfreeonly -immediatetrans TODO wtf */
	LIST_INIT(&res_head);
	/*@ +mustfreeonly +immediatetrans */
	res_rtt_min.tv_sec = 99999;
	res_rtt_min.tv_nsec = 0;
	res_rtt_max.tv_sec = 0;
	res_rtt_max.tv_nsec = 0;
	/*@ -nullstate TODO wtf? */
}
/*@ +nullstate */

void client_res_insert(struct in6_addr *a, data_t *d, ts_t *ts) {
	struct res *r;

	r = malloc(sizeof *r);
	if (r == NULL) return;
	memset(r, 0, sizeof *r);
	(void)clock_gettime(CLOCK_REALTIME, &r->created);
	r->state = STATE_PING;
	memcpy(&r->addr, a, sizeof r->addr);
	r->id = d->id;
	r->seq = d->seq;
	r->ts[0] = *ts;
	/*@ -mustfreeonly -immediatetrans TODO wtf */
	LIST_INSERT_HEAD(&res_head, r, res_list);
	/*@ +mustfreeonly +immediatetrans */
	client_res_update(a, d, ts);
	/*@ -compmempass TODO wtf? */
}
/*@ +compmempass */

void client_res_update(struct in6_addr *a, data_t *d, /*@null@*/ ts_t *ts) {
	struct res *r, *r_tmp;
	ts_t now, diff, rtt;
	char old_state;
	int i;

	(void)clock_gettime(CLOCK_REALTIME, &now);
	r = res_head.lh_first;
	while (r != NULL) {
		/* If match; update */
		old_state = r->state;
		if (r->id == d->id &&
			r->seq == d->seq &&
			memcmp(&r->addr, a, sizeof r->addr) == 0) {
			if (d->type == 'o') {
				if (r->state == STATE_PING)
					r->state = STATE_GOT_PONG;
				if (r->state == STATE_GOT_TS)
					r->state = STATE_READY;
				if (ts != NULL) r->ts[3] = *ts;
				else syslog(LOG_ERR, "client_res: t4 missing");
			} else if (d->type == 't') {
				if (r->state == STATE_PING)
					r->state = STATE_GOT_TS;
				if (r->state == STATE_GOT_PONG)
					r->state = STATE_READY;
				r->ts[1] = d->t2;
				r->ts[2] = d->t3;
			}
		}
		/* Because of Intel RX timestamp bug, wait until next TS to print */
		if (old_state == STATE_READY && d->type == 't') {
			for (i = 0; i < 4; i++)  
				if (r->ts[i].tv_sec == 0 && r->ts[i].tv_nsec == 0) 
					printf("Tstamp   %03d error %d from %d\n", 
						   i, (int)r->seq, (int)r->id);
			diff_ts(&diff, &r->ts[3], &r->ts[0]);
			diff_ts(&now, &r->ts[2], &r->ts[1]);
			diff_ts(&rtt, &diff, &now);
			if (rtt.tv_sec > 0)
				printf("Response %03d from %d in %10ld.%09ld\n", 
					(int)r->seq, (int)r->id, rtt.tv_sec, rtt.tv_nsec);
			else 
				printf("Response %03d from %d in %ld ns\n", 
					(int)r->seq, (int)r->id, rtt.tv_nsec);
			res_response++;
			if (cmp_ts(&res_rtt_max, &rtt) == -1)
			   res_rtt_max = rtt;	
			if (cmp_ts(&res_rtt_min, &rtt) == 1)
			   res_rtt_min = rtt;	
			/* Ready; safe removal */
			r_tmp = r->res_list.le_next;
			/*@ -branchstate -onlytrans TODO wtf */
			LIST_REMOVE(r, res_list);
			free(r);
			r = r_tmp;
			continue;
		}
		diff_ts(&diff, &now, &(r->created)); 
		if (diff.tv_sec > 10) {
			printf("Timeout  %03d from %d in %d sec\n", (int)r->seq, 
					(int)r->id, (int)diff.tv_sec);
			res_timeout++;
			/* Timeout; safe removal */
			r_tmp = r->res_list.le_next;
			/*@ -branchstate -onlytrans TODO wtf */
			LIST_REMOVE(r, res_list);
			/*@ +branchstate +onlytrans */
			free(r);
			r = r_tmp;
			continue;
		}
		/* Alright, next entry */
		r = r->res_list.le_next;
	}
}

void client_res_summary(/*@unused@*/ int sig) {
	float loss;

	loss = (float)res_timeout / (float)res_response;
	printf("\n%d responses, %d timeouts, %f%% packet loss\n", 
			res_response, res_timeout, loss);
	if (res_rtt_max.tv_sec > 0)
		printf("max: %ld.%09ld", res_rtt_max.tv_sec, res_rtt_max.tv_nsec);
	else 
		printf("max: %ld", res_rtt_max.tv_nsec);
	if (res_rtt_min.tv_sec > 0)
		printf(", min: %ld.%09ld\n", res_rtt_min.tv_sec, res_rtt_min.tv_nsec);
	else 
		printf(", min: %ld\n", res_rtt_min.tv_nsec);
	exit(0);
}
