C = gcc
F = -c -Wall -g
H = probed.h
I = -I/usr/include/libxml2
L = -lrt -lxml2
O = $(S:.c=.o)
S = main.c tstamp.c mainloop.c net.c config.c util.c msess.c unix.c client.c
T = probed 
D = -D__gnuc_va_list=va_list +posixlib -mts external/posixlint

all: $T 

$T: $O
	$C $L -o $@ $^

.c.o:
	$C $F $I -o $@ $<

$O: $H

clean:
	rm $T $O 

lint:
	@splint main.c     $I $D 
	@splint net.c      $I $D 
	@splint mainloop.c $I $D
	@splint tstamp.c   $I $D
	@splint util.c     $I $D
	@splint client.c   $I $D
	@splint unix.c   +unixlib
