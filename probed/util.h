void debug(int enabled);
void p(char *str);
void diff_ts(/*@out@*/ ts_t *r, ts_t *end, ts_t *beg);
void diff_tv(struct timeval *r, struct timeval *end, struct timeval *beg);
int cmp_ts(struct timespec *t1, struct timespec *t2);
int cmp_tv(struct timeval *t1, struct timeval *t2);
int addr2str(addr_t *a, /*@out@*/ char *s);
