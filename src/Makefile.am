bin_PROGRAMS = haze

haze_SOURCES = main.c \
               connection-manager.c \
               defines.h \
               connection-manager.h

AM_CPPFLAGS = $(PURPLE_CFLAGS) $(TP_GLIB_CFLAGS)

haze_LDADD = $(PURPLE_LIBS) $(TP_GLIB_LIBS)
