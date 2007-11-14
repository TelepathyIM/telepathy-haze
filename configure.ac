AC_PREREQ([2.59])

# Making releases:
#  set the new version number
#  set nano_version to 0
#  make the release, tag it
#  set nano_version to 1

m4_define([haze_major_version], [0])
m4_define([haze_minor_version], [1])
m4_define([haze_micro_version], [3])
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

AM_INIT_AUTOMAKE([1.8])
AM_CONFIG_HEADER(config.h)

AC_PROG_CC
AC_PROG_CC_STDC
AC_PROG_INSTALL
AC_PROG_LIBTOOL

# decide error flags
AS_COMPILER_FLAG(-Wall, ERROR_CFLAGS="-Wall", ERROR_CFLAGS="")
AS_COMPILER_FLAG(-Werror, werror=yes, werror=no)

AC_ARG_ENABLE(Werror,
  AC_HELP_STRING([--disable-Werror],[compile without -Werror (normally enabled in development builds)]),
    werror=$enableval, :)

AS_COMPILER_FLAG(-Wextra, wextra=yes, wextra=no)
AS_COMPILER_FLAG(-Wno-missing-field-initializers,
        wno_missing_field_initializers=yes,
        wno_missing_field_initializers=no)
AS_COMPILER_FLAG(-Wno-unused-parameter,
        wno_unused_parameter=yes,
        wno_unused_parameter=no)

ifelse(haze_nano_version, 0, [],
    [
        if test x$werror = xyes; then
            ERROR_CFLAGS="$ERROR_CFLAGS -Werror"
        fi
        if test x$wextra = xyes -a \
            x$wno_missing_field_initializers = xyes -a \
            x$wno_unused_parameter = xyes; then
            ERROR_CFLAGS="$ERROR_CFLAGS -Wextra -Wno-missing-field-initializers -Wno-unused-parameter"
        fi
    ])

AC_SUBST(ERROR_CFLAGS)

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
