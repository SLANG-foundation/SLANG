#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/queue.h>
#include <arpa/inet.h>
#include <sqlite3.h>

#include "probed.h"
#include "msess.h"

struct msess *sessions;
struct msess_head sessions_head;
struct timespec timeout;
struct sqlite3 *db;
struct sqlite3_stmt *stmt_insert_probe;

/*
 * Initialize measurement session list handler
 */
void msess_init(void) {

	int rcode;
	char tmpstr[64];

	LIST_INIT(&sessions_head);

	/* set timeout */
	rcode = config_getkey("/config/timeout", &tmpstr[0], sizeof tmpstr);
  if (rcode == 0) {

		syslog(LOG_INFO, "Using timeout %d s", (int)timeout.tv_sec);
		timeout.tv_sec = atoi(tmpstr);
		timeout.tv_nsec = 0;

	} else {

		syslog(LOG_ERR, "Unable to get timeout (%d). Using default.", rcode);
		timeout.tv_sec = 2;
		timeout.tv_nsec = 0;

	}

	/* open sqlite db */
	config_getkey("/config/dbpath", &tmpstr[0], sizeof tmpstr);
	rcode = sqlite3_open(tmpstr, &db);
	if (rcode) {
		syslog(LOG_CRIT, "Unable to open database: %s", sqlite3_errmsg(db));
		sqlite3_close(db);
		die("Unable to open database!");
	}

	/* Prepare SQL statements */
	rcode = sqlite3_prepare_v2(db, "INSERT INTO probes (\
		session_id, seq, \
		t1_sec, t1_nsec, \
		t2_sec, t2_nsec, \
		t3_sec, t3_nsec, \
		t4_sec, t4_nsec, \
		state\
		) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);", 
		-1, &stmt_insert_probe, NULL);
	if (rcode != SQLITE_OK) {
		syslog(LOG_ERR, "Unable to prepare statement stmt_insert_probe: %s", sqlite3_errmsg(db));
	}

}

/*
 * Add measurement session to list
 */
struct msess *msess_add(void) {

	struct msess *s;

	s = malloc(sizeof (struct msess));
	memset(s, 0, sizeof (struct msess));

	LIST_INIT(&(s->probes));
	LIST_INSERT_HEAD(&sessions_head, s, entries);

	// add error check
	s->id = msess_next_id();

	return s;
	
}

/*
 * Remove measurement session from list
 */
void msess_remove(struct msess *sess) {

	struct msess_probe *p;
	
	/* remove probes */
	for (p = sess->probes.lh_first; p != NULL; p = p->entries.le_next) {
		msess_probe_remove(p);
	}

	/* remove msess */
	LIST_REMOVE(sess, entries);
	free(sess);

	/* remove session result from database? */

} 

/*
 * Find a msess entry for the given address and ID
 */
struct msess *msess_find(msess_id id) {

	struct msess *sess;

	/* iterate all sessions and compare IDs */
	for (sess = sessions_head.lh_first; sess != NULL; sess = sess->entries.le_next) {

		if (sess->id == id) {
			return sess;
		}

	}

	// no msess found
	return NULL;

}

/*
 * Find next free id
 */
msess_id msess_next_id(void) {

	struct msess *sess;
	msess_id i;
	char free;

	for (i = 0; i < (msess_id)-1; i++) {

		free = 1;

		for (sess = sessions_head.lh_first; sess != NULL; sess = sess->entries.le_next) {

			if (sess->id == i) {
				free = 0;
			}
			//free |= (sess->id == i);
		}

		if (free) {
			return i;
		}
		
	}
	
	// all IDs in use
	return 0;

}

/*
 * Handle a new timestamp
 */
void msess_add_ts(struct msess *sess, uint32_t seq, enum TS_TYPES tstype, struct timeval *ts) {

	struct msess_probe *p;

	/* if we receive a t1, we have a new session */
	if (tstype == T1) {

		p = malloc(sizeof (struct msess_probe));
		memset(p, 0, sizeof (struct msess_probe));

		p->seq = seq;
		memcpy(&(p->t1), ts, sizeof (struct timeval));

		LIST_INSERT_HEAD(&(sess->probes), p, entries);
			
	} else {

		// find session
		for (p = sess->probes.lh_first; p != NULL; p = p->entries.le_next) {
			if (p->seq == seq) {

				// set correct timestamp
				switch (tstype) {
					case T1: /* needed to make gcc shut up */
						break;
					case T2:
						memcpy(&(p->t2), ts, sizeof (struct timeval));
						break;
					case T3:
						memcpy(&(p->t3), ts, sizeof (struct timeval));
						break;
					case T4:
						memcpy(&(p->t4), ts, sizeof (struct timeval));
						break;
				}

			}
		}
		
		// Session not found! Log error.

	}

}

/*
 * Remove a probe
 */
void msess_probe_remove(struct msess_probe *p) {

	LIST_REMOVE(p, entries);
	free(p);

}

/*
 * Save a probe to database
 */
void msess_probe_save(struct msess_probe *p) {
	
	

}

/*
 * Prints all sessions currently configured to console
 */
void msess_print_all(void) {

	struct msess *sess;

	for (sess = sessions_head.lh_first; sess != NULL; sess = sess->entries.le_next) {
		msess_print(sess);
	}

}

/*
 * Print one session to console
 */
void msess_print(struct msess *sess) {

	char addr_str[INET6_ADDRSTRLEN];
	struct sockaddr_in6 *addr;
	struct msess_probe *p;

	addr = (struct sockaddr_in6 *)&(sess->dst);
	inet_ntop(AF_INET6, addr->sin6_addr.s6_addr, addr_str, INET6_ADDRSTRLEN);
	printf("Measurement session ID %d:\n", sess->id);
	printf(" Destination address: %s\n", addr_str);
	printf(" Destination port: %d\n", ntohs(addr->sin6_port));
	printf(" Interval: %d\n", sess->interval_usec);
	printf(" Current state:\n");

	for (p = sess->probes.lh_first; p != NULL; p = p->entries.le_next) {

		printf("  Sequence: %d\n", p->seq);
		printf("  T1 sec: %d usec: %d\n", (int)p->t1.tv_sec, (int)p->t1.tv_nsec);
		printf("  T2 sec: %d usec: %d\n", (int)p->t2.tv_sec, (int)p->t2.tv_nsec);
		printf("  T3 sec: %d usec: %d\n", (int)p->t3.tv_sec, (int)p->t3.tv_nsec);
		printf("  T4 sec: %d usec: %d\n", (int)p->t4.tv_sec, (int)p->t4.tv_nsec);
		printf("\n");

	}
	
	printf("\n");

}

/*
 * Flush completed probes (to database?)
 * Returns number of entries saved to database.
 */
int msess_flush(void) {

	struct msess *sess;
	struct msess_probe *p;
	struct timespec c_now;
	struct timespec c_diff;
	int save, state, nsaved = 0;

	clock_gettime(CLOCK_REALTIME, &c_now);

	/* begin transaction -- ADD ERROR CHECK */
	sqlite3_exec(db, "BEGIN;", NULL, NULL, NULL);

	// find completed probe runs
	for (sess = sessions_head.lh_first; sess != NULL; sess = sess->entries.le_next) {

		save = 0;
		state = PROBE_STATE_SUCCESS;

		for (p = sess->probes.lh_first; p != NULL; p = p->entries.le_next) {
			
			/* Timed out? */
			diff_ts(&c_diff, &c_now, &p->t1);

			if (cmp_ts(&c_diff, &timeout) == 1) {
				state = PROBE_STATE_TIMEOUT;
				save = 1;
			}

			/* Got all timestamps? */
			if (p->t1.tv_sec && p->t2.tv_sec && p->t3.tv_sec && p->t4.tv_sec) {
				state = PROBE_STATE_SUCCESS;
				save = 1;
			}

			/* save probe, if needed */
			if (save) {

				sqlite3_bind_int(stmt_insert_probe, 0, sess->id);
				sqlite3_bind_int(stmt_insert_probe, 1, p->seq);
				sqlite3_bind_int(stmt_insert_probe, 2, p->t1.tv_sec);
				sqlite3_bind_int(stmt_insert_probe, 3, p->t1.tv_nsec);
				sqlite3_bind_int(stmt_insert_probe, 4, p->t2.tv_sec);
				sqlite3_bind_int(stmt_insert_probe, 5, p->t2.tv_nsec);
				sqlite3_bind_int(stmt_insert_probe, 6, p->t3.tv_sec);
				sqlite3_bind_int(stmt_insert_probe, 7, p->t3.tv_nsec);
				sqlite3_bind_int(stmt_insert_probe, 8, p->t4.tv_sec);
				sqlite3_bind_int(stmt_insert_probe, 9, p->t4.tv_nsec);
				sqlite3_bind_int(stmt_insert_probe, 9, state);

				if (sqlite3_step(stmt_insert_probe) != SQLITE_OK) {
					/* RATE LIMIT IN SOME WAY! */
					syslog(LOG_ERR, "Unable to save data, data lost: %s", sqlite3_errmsg(db));
				}

				sqlite3_reset(stmt_insert_probe);
				msess_probe_remove(p);
				nsaved++;
				
			}
	
		}

		/* commit transaction -- ADD ERROR CHECK */
		sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);

	} 

	return nsaved;

}
