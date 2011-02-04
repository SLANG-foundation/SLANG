int recv_w_ts(int sock, int flags, /*@out@*/ struct packet *pkt);
int send_w_ts(int sock, addr_t *addr, char *data, /*@out@*/ ts_t *ts);
int dscp_set(int sock, uint8_t dscp);
int dscp_extract(struct msghdr *msg, /*@out@*/ uint8_t *dscp_out);
