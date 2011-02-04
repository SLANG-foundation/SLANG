#!/bin/sh
if [ "$1" = "clean" ]
then
	make clean
	make distclean
	rm depcomp configure missing
	rm Makefile.in probed/Makefile.in 
	rm install-sh aclocal.m4 
	rm -r autom4te.cache 
	exit 
fi
if [ "$1" = "install" ]
then
	autoreconf --install
	exit
fi
if [ "$1" = "lint" ]
then
	cd probed
	I="-I/usr/include/libxml2"
	D="-D__gnuc_va_list=va_list +posixlib -mts external/posixlint"
	splint main.c net.c loop.c tstamp.c util.c client.c $I $D 
	splint unix.c   +unixlib
	exit 
fi
echo "$0 clean | install | lint"
