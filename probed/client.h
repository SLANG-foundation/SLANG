pid_t client_fork(int pipe, struct sockaddr_in6 *server);
void client_res_init(void);
void client_res_insert(struct in6_addr *a, data_t *d, ts_t *ts);
void client_res_update(struct in6_addr *a, data_t *d, /*@null@*/ ts_t *ts);
void client_res_summary(/*@unused@*/ int sig);
