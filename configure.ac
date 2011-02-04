AC_INIT([SLA-NG], [1.0])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
#AM_SILENT_RULES([yes])
AC_PROG_CC
AC_CHECK_FUNCS([clock_gettime])
PKG_CHECK_MODULES([XML2],[libxml-2.0])
AC_CONFIG_FILES([
 Makefile
 probed/Makefile
])
AC_OUTPUT
