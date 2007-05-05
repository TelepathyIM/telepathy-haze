AC_INIT([haze],[0.0.1],[resiak@soc.pidgin.im])
AM_INIT_AUTOMAKE([AC_PACKAGE_NAME],[AC_PACKAGE_VERSION])
AC_PROG_CC
PKG_CHECK_MODULES([PURPLE],[purple],[],[AC_MSG_ERROR([
* purple is needed to build haze.])])
PKG_CHECK_MODULES([TP_GLIB],[telepathy-glib],[],[AC_MSG_ERROR([
* telepathy-glib is needed to build haze.])])
AC_OUTPUT([src/Makefile
           ])
