C = gcc
F = -c -Wall -I .. -I /usr/include/libxml2 -g
S = msess_t.c ../msess.c 
I = ../msess.h
L = 
O = $(S:.c=.o)
T = msess_t

all: $T 

$T: $O
	$C $L -o $@ $^

.c.o:
	$C $F -o $@ $<

$O: $I

clean:
	rm $T $O 
