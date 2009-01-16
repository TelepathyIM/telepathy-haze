#!/bin/sh

abs_top_srcdir="$1"
shift
abs_top_builddir="$1"
shift

cd "${abs_top_builddir}/tests"

export HAZE_DEBUG=all
ulimit -c unlimited
exec >> haze-testing.log 2>&1

if test -n "$HAZE_TEST_VALGRIND"; then
        export G_DEBUG=${G_DEBUG:+"${G_DEBUG},"}gc-friendly
        export G_SLICE=always-malloc
        HAZE_WRAPPER="valgrind --leak-check=full --num-callers=20"
        HAZE_WRAPPER="$HAZE_WRAPPER --show-reachable=yes"
        HAZE_WRAPPER="$HAZE_WRAPPER --gen-suppressions=all"
        HAZE_WRAPPER="$HAZE_WRAPPER --child-silent-after-fork=yes"
        HAZE_WRAPPER="$HAZE_WRAPPER --suppressions=${abs_top_srcdir}/tools/tp-glib.supp"
elif test -n "$HAZE_TEST_REFDBG"; then
        if test -z "$REFDBG_OPTIONS" ; then
                export REFDBG_OPTIONS="btnum=10"
        fi
        if test -z "$HAZE_WRAPPER" ; then
                HAZE_WRAPPER="refdbg"
        fi
fi

# not suitable for haze:
#export G_DEBUG=fatal-warnings" ${G_DEBUG}"
exec "${abs_top_builddir}/libtool" --mode=execute $HAZE_WRAPPER \
    "${abs_top_builddir}/src/telepathy-haze"
