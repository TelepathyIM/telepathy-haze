AC_PREREQ([2.59])

# Making releases:
#  set the new version number
#  set nano_version to 0
#  make the release, tag it
#  set nano_version to 1

m4_define([haze_major_version], [0])
m4_define([haze_minor_version], [7])
m4_define([haze_micro_version], [0])
m4_define([haze_nano_version], [0])

m4_define([haze_base_version],
          [haze_major_version.haze_minor_version.haze_micro_version])
m4_define([haze_version],
          [m4_if(haze_nano_version, 0,
                 [haze_base_version],
                 [haze_base_version].[haze_nano_version])])

AC_INIT([telepathy-haze], [haze_version],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=Telepathy&component=haze])

AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([1.9 -Wno-portability])
AM_CONFIG_HEADER([config.h])
AC_CONFIG_FILES([Makefile
		 src/Makefile
		 data/Makefile
		 m4/Makefile
		 tests/Makefile
		 tests/twisted/Makefile
		 tools/Makefile
		 extensions/Makefile
		 ])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES])

AC_PROG_CC
AC_PROG_CC_STDC
AC_PROG_INSTALL
AC_PROG_LIBTOOL

COMPILER_OPTIMISATIONS
COMPILER_COVERAGE
LINKER_OPTIMISATIONS

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

AC_CHECK_HEADERS_ONCE([libintl.h])

AC_ARG_ENABLE(leaky-request-stubs,
  AC_HELP_STRING([--enable-leaky-request-stubs],[print debugging information when libpurple attempts to use the request API (warning: very leaky)]),
  AC_DEFINE(ENABLE_LEAKY_REQUEST_STUBS, [], [Enable the leaky stub implementation of the request API for debugging purposes]))
AC_SUBST(ENABLE_LEAKY_REQUEST_STUBS)

PKG_CHECK_MODULES(PURPLE,[purple >= 2.7])
PKG_CHECK_MODULES(TP_GLIB,[telepathy-glib >= 0.15.1])
PKG_CHECK_MODULES(GLIB,[glib-2.0 >= 2.22, gobject-2.0, gio-2.0])
PKG_CHECK_MODULES(DBUS_GLIB,[dbus-glib-1 >= 0.73])

GLIB_GENMARSHAL=`$PKG_CONFIG --variable=glib_genmarshal glib-2.0`
AC_SUBST(GLIB_GENMARSHAL)

AC_CHECK_LIB(purple, purple_dbus_uninit,
  [ AC_DEFINE(HAVE_PURPLE_DBUS_UNINIT, [],
    [Define if purple_dbus_uninit is present in libpurple]) ],
  [], [$PURPLE_LIBS])

dnl Check for code generation tools
XSLTPROC=
AC_CHECK_PROGS([XSLTPROC], [xsltproc])
if test -z "$XSLTPROC"; then
  AC_MSG_ERROR([xsltproc (from the libxslt source package) is required])
fi

AM_PATH_PYTHON([2.5])

# Check for a Python >= 2.5 with Twisted, to run the tests
AC_MSG_CHECKING([for Python with Twisted and XMPP protocol support])
if $PYTHON -c "import twisted.words.xish.domish, twisted.words.protocols.jabber, twisted.internet.reactor" >/dev/null 2>&1; then
    TEST_PYTHON="$PYTHON"
else
    TEST_PYTHON=false
fi
AC_MSG_RESULT([$TEST_PYTHON])
AC_SUBST(TEST_PYTHON)
AM_CONDITIONAL([WANT_TWISTED_TESTS], test false != "$TEST_PYTHON")

AC_ARG_ENABLE(media,
  AC_HELP_STRING([--disable-media],[disable audio/video calls]),
  [
    AM_CONDITIONAL([MEDIA_ENABLED], false)
  ],[
    AC_DEFINE(ENABLE_MEDIA, [], [Enable audio/video calls])
    AM_CONDITIONAL([MEDIA_ENABLED], true)
  ])
AC_SUBST(ENABLE_MEDIA)

#AS_AC_EXPAND(DATADIR, $datadir)
#DBUS_SERVICES_DIR="$DATADIR/dbus-1/services"
#AC_SUBST(DBUS_SERVICES_DIR)
#AC_DEFINE_UNQUOTED(DBUS_SERVICES_DIR, "$DBUS_SERVICES_DIR", [DBus services directory])

AC_OUTPUT
