void tstamp_mode_hardware(int sock, char *iface);
void tstamp_mode_kernel(int sock);
void tstamp_mode_userland(int sock);
int tstamp_extract(struct msghdr *msg, /*@out@*/ ts_t *ts);
int tstamp_fetch_tx(int sock, /*@out@*/ ts_t *ts);
