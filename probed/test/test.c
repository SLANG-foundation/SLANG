#include <stdlib.h>
#include "test.h"

/* 
 * calculate usec precision diff for positive time
 */
void diff_tv (struct timeval *r, struct timeval *end, struct timeval *beg) {
	if ((end->tv_usec - beg->tv_usec) < 0) {
		r->tv_sec = end->tv_sec - beg->tv_sec - 1;
		r->tv_usec = 1000000 + end->tv_usec - beg->tv_usec;
	} else {
		r->tv_sec = end->tv_sec - beg->tv_sec;
		r->tv_usec = end->tv_usec - beg->tv_usec;
	}
}

/*
 * Randomize order of array
 */
void shake(int *arr, int length) {

	int *tmp, rnd, i;
	
	for (i = 0; i < length; i++) {

		rnd = random() % (length - i);
		tmp = &arr[i];
		arr[i] = arr[i+rnd];
		arr[i+rnd] = &tmp;
		
	}

}
