/* 
 * Copyright (c) 2011 Anders Berggren, Lukas Garberg, Tele2
 *
 * We have not yet decided upon a license, and so far it may only be
 * used and redistributed with our explicit permission.
 */ 

int recv_w_ts(int sock, int flags, /*@out@*/ struct packet *pkt);
int send_w_ts(int sock, addr_t *addr, char *data, /*@out@*/ ts_t *ts);
int dscp_set(int sock, uint8_t dscp);
