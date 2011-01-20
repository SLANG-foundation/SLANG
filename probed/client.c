#include <string.h>
#include <time.h>
#include <sys/queue.h>
#include "probed.h"

void client_res_init(void) {
	LIST_INIT(&res_head);
}

void client_res_insert(struct in6_addr *addr, data_t *data, ts_t *ts) {
	struct res *r;
	r = malloc(sizeof (struct res));
	memset(r, 0, sizeof (struct res));
	r->state = STATE_PING;
	memcpy(&(r->addr), addr, sizeof addr);
	r->id = data->id;
	r->seq = data->seq;
	r->ts[0] = *ts;
	LIST_INSERT_HEAD(&res_head, r, res_list);
}

void client_res_update(struct in6_addr *a, data_t *data, ts_t *ts) {
	struct res *r, *r_tmp;
	ts_t now, diff;
	char old_state;

	(void)clock_gettime(CLOCK_REALTIME, &now);
	r = res_head.lh_first;
	while (r != NULL) {
		/* If match; update */
		old_state = r->state;
		if (r->id == data->id &&
			r->seq == data->seq &&
			memcmp(&(r->addr), a, sizeof a) == 0) {
			if (data->type == 'o') {
				if (r_tmp->state == STATE_PING)
					r_tmp->state = STATE_GOT_PONG;
				if (r_tmp->state == STATE_GOT_TS)
					r_tmp->state = STATE_READY;
				r_tmp->ts[3] = *ts;
			} else {
				if (r_tmp->state == STATE_PING)
					r_tmp->state = STATE_GOT_TS;
				if (r_tmp->state == STATE_GOT_PONG)
					r_tmp->state = STATE_READY;
				r_tmp->ts[1] = data->t2;
				r_tmp->ts[2] = data->t3;
			}
		}
		/* Because of Intel RX timestamp bug, wait until next TS to print */
		if (old_state == STATE_READY && data->type == 't') {
			/* Ready; safe removal */
			syslog(LOG_INFO, "Response %d", r->id);
			r_tmp = r->res_list.le_next;
			LIST_REMOVE(r, res_list);
			free(r);
			r = r_tmp;
			continue;
		}
		diff_ts(&diff, &now, &(r->ts[0])); 
		if (diff.tv_sec > 10) {
			/* Timeout; safe removal */
			syslog(LOG_INFO, "Timeout %d, %d sec", r->id, diff.tv_sec);
			r_tmp = r->res_list.le_next;
			LIST_REMOVE(r, res_list);
			free(r);
			r = r_tmp;
			continue;
		}
		/* Alright, next entry */
		r = r->res_list.le_next;
	}
}

void client_res_print() {
	struct res *r;

	r = res_head.lh_first;
	while (r != NULL) {
		printf("id: %d, seq: %d\n", r->id, r->seq);
		r = r->res_list.le_next;
	}
}
