void bind_or_die(/*@out@*/ int *s_udp, /*@out@*/ int *s_tcp, uint16_t port);
void loop_or_die(int s_udp, int s_tcp);
int server_find_peer_fd(int fd_first, int fd_max, addr_t *peer);
