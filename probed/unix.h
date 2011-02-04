void unix_fd_set(int sock, /*@out@*/ fd_set *fs);
void unix_fd_zero(/*@out@*/ fd_set *fs);
int unix_fd_isset(int sock, /*@out@*/ fd_set *fs);
