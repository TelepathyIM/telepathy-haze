libexec_PROGRAMS = telepathy-haze

man_MANS = telepathy-haze.8

%.8: %.8.in Makefile
	sed -e 's,[@]libexecdir[@],@libexecdir@,' < $< > $@

EXTRA_DIST = telepathy-haze.8.in
CLEANFILES = $(man_MANS)

telepathy_haze_SOURCES = main.c \
                         defines.h \
                         debug.c \
                         debug.h \
                         connection-manager.c \
                         connection-manager.h \
                         connection-aliasing.c \
                         connection-aliasing.h \
                         connection-avatars.c \
                         connection-avatars.h \
                         connection-presence.c \
                         connection-presence.h \
                         connection.c \
                         connection.h \
                         contact-list.c \
                         contact-list.h \
                         contact-list-channel.c \
                         contact-list-channel.h \
                         im-channel.h \
                         im-channel.c \
                         im-channel-factory.c \
                         im-channel-factory.h \
                         notify.c \
                         notify.h \
                         request.c \
                         request.h

AM_CFLAGS = \
	-DG_LOG_DOMAIN=\"haze\" \
	$(ERROR_CFLAGS) \
	@PURPLE_CFLAGS@ \
	@TP_GLIB_CFLAGS@ \
	@DBUS_GLIB_CFLAGS@

AM_LDFLAGS = @PURPLE_LIBS@ @TP_GLIB_LIBS@ @DBUS_GLIB_CFLAGS@
