AC_INIT([probed], [1.0], [anders@halon.se or lukas@spritelink.net])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_PROG_CC
AC_CHECK_FUNCS([clock_gettime])
PKG_CHECK_MODULES([XML2],[libxml-2.0])
AC_CONFIG_FILES([
 Makefile
 probed/Makefile
])
AC_OUTPUT
