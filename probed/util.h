/* 
 * Copyright (c) 2011 Anders Berggren, Lukas Garberg, Tele2
 *
 * We have not yet decided upon a license, and so far it may only be
 * used and redistributed with our explicit permission.
 */ 

#define MAX(x, y) ((x)>(y)?(x):(y))

void debug(int enabled);
void p(char *str);
int diff_ts(/*@out@*/ ts_t *r, ts_t *a, ts_t *b);
int cmp_ts(struct timespec *t1, struct timespec *t2);
int cmp_tv(struct timeval *t1, struct timeval *t2);
int addr2str(addr_t *a, /*@out@*/ char *s);
