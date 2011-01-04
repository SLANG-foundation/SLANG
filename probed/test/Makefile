C = cc
F = -O0 -c -Wall -I .. -I /usr/include/libxml2 -g
#F = -pg -fprofile-arcs -ftest-coverage -c -Wall -I .. -I /usr/include/libxml2 -g
S = test.c msess_t.c ../msess.c
I = ../msess.h
L = 
#L = -pg -fprofile-arcs -ftest-coverage
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
