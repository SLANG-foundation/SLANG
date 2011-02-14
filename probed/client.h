void client_init(void);
void client_res_fifo_or_die(char *fifopath);
void client_res_update(struct in6_addr *a, data_t *d, /*@null@*/ ts_t *ts);
void client_res_summary(/*@unused@*/ int sig);
void client_msess_transmit(int s_udp);
void client_msess_forkall(int pipe);
int client_msess_reconf(char *port, char *cfgpath);
int client_msess_add(char *port, char *a, uint8_t dscp, int wait, uint16_t id);
int client_msess_gothello(addr_t *addr);
