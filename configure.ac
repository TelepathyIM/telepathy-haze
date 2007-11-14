AC_PREREQ([2.59])

AC_INIT([telepathy-haze],[0.1.3],[https://bugs.freedesktop.org/enter_bug.cgi?product=Telepathy&component=telepathy-haze])

AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.8])
AM_CONFIG_HEADER(config.h)

AC_PROG_CC
AC_PROG_CC_STDC
AC_PROG_INSTALL
AC_PROG_LIBTOOL

PKG_CHECK_MODULES(PURPLE,[purple >= 2.0.0])
AC_SUBST(PURPLE_CFLAGS)
AC_SUBST(PURPLE_LIBS)

PKG_CHECK_MODULES(TP_GLIB,[telepathy-glib])
AC_SUBST(TP_GLIB_CFLAGS)
AC_SUBST(TP_GLIB_LIBS)

AC_CHECK_LIB(telepathy-glib, tp_debug_set_flags,
  [ AC_DEFINE(HAVE_TP_DEBUG_SET_FLAGS, [],
    [Define if tp_debug_set_flags() function is provided by telepathy-glib]) ],
  [], [$TP_GLIB_LIBS])

#AS_AC_EXPAND(DATADIR, $datadir)
#DBUS_SERVICES_DIR="$DATADIR/dbus-1/services"
#AC_SUBST(DBUS_SERVICES_DIR)
#AC_DEFINE_UNQUOTED(DBUS_SERVICES_DIR, "$DBUS_SERVICES_DIR", [DBus services directory])

AC_OUTPUT([Makefile
           src/Makefile
           data/Makefile
           m4/Makefile
           ])
