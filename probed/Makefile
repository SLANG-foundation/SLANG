C = gcc
F = -c -Wall -I/usr/include/libxml2 -g
L = -lrt -lxml2
#S = main.c proto.c tstamp.c data.c config.c util.c msess.c
S = main.c tstamp.c udp.c data.c config.c util.c msess.c
I = probed.h
O = $(S:.c=.o)
T = probed 

all: $T 

$T: $O
	$C $L -o $@ $^

.c.o:
	$C $F -o $@ $<

$O: $I

clean:
	rm $T $O 
