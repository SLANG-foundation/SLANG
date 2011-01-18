/*
 * UNIX wrapper functions
 * These are wrappers for UNIX functions that cannot be 
 * analysed with POSIX definitions only. Optimally, everything
 * should be checked with +unixlib, but because of SPlint bugs, 
 * that simply doesn't work.
 * 
 * This file is checked with `splint unix.c +unixlib`
 */ 

#include <sys/select.h>

void unix_fd_set(int sock, fd_set *fs) {
	FD_SET(sock, fs);
}

void unix_fd_zero(fd_set *fs) {
	FD_ZERO(fs);
}

int unix_fd_isset(int sock, fd_set *fs) {
	if (FD_ISSET(sock, fs)) return 1;
	else return 0;
}
