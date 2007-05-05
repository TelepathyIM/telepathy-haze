bin_PROGRAMS = haze

haze_SOURCES = haze.c \
               haze-connection-manager.c \
               defines.h \
               haze-connection-manager.h

AM_CPPFLAGS = $(PURPLE_CFLAGS) $(TP_GLIB_CFLAGS)

haze_LDADD = $(PURPLE_LIBS) $(TP_GLIB_LIBS)
