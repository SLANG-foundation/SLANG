C = gcc
F = -c -Wall -g
H = probed.h
I = -I/usr/include/libxml2
L = -lrt -lxml2
O = $(S:.c=.o)
S = main.c tstamp.c udp.c data.c config.c util.c msess.c unix.c client.c
T = probed 
D = -D__gnuc_va_list=va_list

all: $T 

$T: $O
	$C $L -o $@ $^

.c.o:
	$C $F $I -o $@ $<

$O: $H

clean:
	rm $T $O 

lint:
	@splint main.c   $I $D +posixlib -mts posixlint 
	@splint data.c   $I $D +posixlib -mts posixlint
	@splint udp.c    $I $D +posixlib -mts posixlint
	@splint tstamp.c $I $D +posixlib -mts posixlint
	@splint util.c   $I $D +posixlib -mts posixlint
	@splint client.c $I $D +posixlib -mts posixlint
	@splint unix.c         +unixlib
