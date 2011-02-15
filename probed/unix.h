/* 
 * Copyright (c) 2011 Anders Berggren, Lukas Garberg, Tele2
 *
 * We have not yet decided upon a license, and so far it may only be
 * used and redistributed with our explicit permission.
 */ 

void unix_fd_set(int sock, /*@out@*/ fd_set *fs);
void unix_fd_clr(int sock, /*@out@*/ fd_set *fs);
void unix_fd_zero(/*@out@*/ fd_set *fs);
int unix_fd_isset(int sock, /*@out@*/ fd_set *fs);
