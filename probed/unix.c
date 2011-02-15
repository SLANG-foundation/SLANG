/* 
 * Copyright (c) 2011 Anders Berggren, Lukas Garberg, Tele2
 *
 * We have not yet decided upon a license, and so far it may only be
 * used and redistributed with our explicit permission.
 */ 

/**
 * \file   unix.c
 * \brief  UNIX wrapper functions (due to SPlint problem)
 * \author Anders Berggren
 * \author Lukas Garberg

 * These are wrappers for UNIX functions that cannot be 
 * analysed with POSIX definitions only. Optimally, everything
 * should be checked with +unixlib, but because of SPlint bugs, 
 * that simply doesn't work.
 *
 * This file is checked with `splint unix.c +unixlib`
 */ 

#include <sys/select.h>

/**
 * Used like FD_SET
 */
void unix_fd_set(int sock, fd_set *fs) {
	FD_SET(sock, fs);
}

/**
 * Used like FD_CLR
 */
void unix_fd_clr(int sock, fd_set *fs) {
	FD_CLR(sock, fs);
}

/**
 * Used like FD_ZERO
 */
void unix_fd_zero(fd_set *fs) {
	FD_ZERO(fs);
}

/**
 * Used like FD_ISSET
 */
int unix_fd_isset(int sock, fd_set *fs) {
	if (FD_ISSET(sock, fs)) return 1;
	else return 0;
}
