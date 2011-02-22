#!/bin/sh
if [ "$1" = "clean" ]
then
	make clean
	make distclean
	rm depcomp configure missing
	rm Makefile.in probed/Makefile.in 
	rm install-sh aclocal.m4 
	rm -r autom4te.cache
	rm manager/*.pyc
	rm manager/slang/*.pyc
	rm sla-ng.deb
	exit 
fi
if [ "$1" = "install" ]
then
	autoreconf --install
	./configure
	make
	exit
fi
if [ "$1" = "debian" ]
then
	$0 install
	cp probed/probed debian/usr/bin
	fakeroot dpkg-deb --build debian
	mv debian.deb sla-ng.deb
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
if [ "$1" = "release" ]
then
	rm ../slang.tgz
	$0 clean
	$0 install
	rm -r autom4te.cache
	tar -C ../ -zcf ../slang.tgz slang 
	exit 
fi
echo "$0 clean | install | lint | release"
