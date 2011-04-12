/* 
 * Copyright (c) 2011 Anders Berggren, Lukas Garberg, Tele2
 *
 * We have not yet decided upon a license, and so far it may only be
 * used and redistributed with our explicit permission.
 */ 

void client_init(void);
void client_send_fork(int pipe);
void client_res_fifo_or_die(char *fifopath);
void client_res_update(addr_t *a, data_t *d, /*@null@*/ ts_t *ts, int dscp);
void client_res_summary(/*@unused@*/ int sig);
void client_res_clear_timeouts(void);
void client_msess_transmit(int s_udp, int sends);
void client_msess_forkall(int pipe);
int client_msess_reconf(char *port, char *cfgpath);
int client_msess_add(char *port, char *a, uint8_t dscp, int wait, num_t id);
int client_msess_gothello(addr_t *addr);
