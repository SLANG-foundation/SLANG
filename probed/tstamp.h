/* 
 * Copyright (c) 2011 Anders Berggren, Lukas Garberg, Tele2
 *
 * We have not yet decided upon a license, and so far it may only be
 * used and redistributed with our explicit permission.
 */ 

void tstamp_mode_hardware(int sock, char *iface);
void tstamp_mode_kernel(int sock);
void tstamp_mode_userland(int sock);
int tstamp_extract(struct msghdr *msg, /*@out@*/ ts_t *ts, int tx);
int tstamp_fetch_tx(int sock, /*@out@*/ ts_t *ts);
