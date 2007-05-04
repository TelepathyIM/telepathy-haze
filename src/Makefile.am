bin_PROGRAMS = psyduck

psyduck_SOURCES = psyduck.c \
                  psyduck-connection-manager.c \
                  defines.h \
                  psyduck-connection-manager.h

AM_CPPFLAGS = $(PURPLE_CFLAGS) $(TP_GLIB_CFLAGS)

psyduck_LDADD = $(PURPLE_LIBS) $(TP_GLIB_LIBS)
