AC_PREREQ([2.59])

# Making releases:
#  set the new version number
#  set nano_version to 0
#  make the release, tag it
#  set nano_version to 1

m4_define([haze_major_version], [0])
m4_define([haze_minor_version], [3])
m4_define([haze_micro_version], [2])
m4_define([haze_nano_version], [0])

m4_define([haze_base_version],
          [haze_major_version.haze_minor_version.haze_micro_version])
m4_define([haze_version],
          [m4_if(haze_nano_version, 0,
                 [haze_base_version],
                 [haze_base_version].[haze_nano_version])])

AC_INIT([telepathy-haze], [haze_version],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=Telepathy&component=telepathy-haze])

AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.9 -Wno-portability])
AM_CONFIG_HEADER(config.h)

AC_PROG_CC
AC_PROG_CC_STDC
AC_PROG_INSTALL
AC_PROG_LIBTOOL

ifelse(haze_nano_version, 0, [release=yes], [release=no])
TP_COMPILER_WARNINGS([ERROR_CFLAGS], [test x$release = xno],
  [all \
   extra \
   declaration-after-statement \
   shadow \
   strict-prototypes \
   missing-prototypes \
   sign-compare \
   nested-externs \
   pointer-arith \
   format-security \
   init-self],
  [missing-field-initializers \
   unused-parameter])
AC_SUBST(ERROR_CFLAGS)

AC_ARG_ENABLE(leaky-request-stubs,
  AC_HELP_STRING([--enable-leaky-request-stubs],[print debugging information when libpurple attempts to use the request API (warning: very leaky)]),
  AC_DEFINE(ENABLE_LEAKY_REQUEST_STUBS, [], [Enable the leaky stub implementation of the request API for debugging purposes]))
AC_SUBST(ENABLE_LEAKY_REQUEST_STUBS)

PKG_CHECK_MODULES(PURPLE,[purple >= 2.1.1])
AC_SUBST(PURPLE_CFLAGS)
AC_SUBST(PURPLE_LIBS)

PKG_CHECK_MODULES(TP_GLIB,[telepathy-glib >= 0.7.21])
AC_SUBST(TP_GLIB_CFLAGS)
AC_SUBST(TP_GLIB_LIBS)

PKG_CHECK_MODULES(DBUS_GLIB,[dbus-glib-1 >= 0.73])
AC_SUBST(DBUS_GLIB_CFLAGS)
AC_SUBST(DBUS_GLIB_LIBS)

AC_CHECK_LIB(purple, purple_dbus_uninit,
  [ AC_DEFINE(HAVE_PURPLE_DBUS_UNINIT, [],
    [Define if purple_dbus_uninit is present in libpurple]) ],
  [], [$PURPLE_LIBS])

# Check for a Python >= 2.5 with Twisted, to run the tests
AC_MSG_CHECKING([for Python >= 2.5 with Twisted and XMPP protocol support])
for TEST_PYTHON in python2.5 python2.6 python; do
  if $TEST_PYTHON -c "from sys import version_info; import dbus, dbus.mainloop.glib; raise SystemExit(version_info < (2, 5, 0, 'final', 0))" >/dev/null 2>&1; then
    if $TEST_PYTHON -c "import twisted.words.xish.domish, twisted.words.protocols.jabber, twisted.internet.reactor" >/dev/null 2>&1; then
      AC_MSG_RESULT([$TEST_PYTHON])
      AM_CONDITIONAL([WANT_TWISTED_TESTS], true)
      break
    else
      TEST_PYTHON=false
    fi
  fi
done
AC_SUBST(TEST_PYTHON)
AM_CONDITIONAL([WANT_TWISTED_TESTS], test false != "$TEST_PYTHON")

#AS_AC_EXPAND(DATADIR, $datadir)
#DBUS_SERVICES_DIR="$DATADIR/dbus-1/services"
#AC_SUBST(DBUS_SERVICES_DIR)
#AC_DEFINE_UNQUOTED(DBUS_SERVICES_DIR, "$DBUS_SERVICES_DIR", [DBus services directory])

SHAVE_INIT(.)

AC_OUTPUT([Makefile
           src/Makefile
           data/Makefile
           m4/Makefile
	   tests/Makefile
	   tests/twisted/Makefile
	   tools/Makefile
           shave
           shave-libtool
           ])
