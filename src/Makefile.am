libexec_PROGRAMS = telepathy-haze

man_MANS = telepathy-haze.8

%.8: %.8.in Makefile
	$(AM_V_GEN)sed -e 's,[@]libexecdir[@],@libexecdir@,' < $< > $@

EXTRA_DIST = telepathy-haze.8.in
CLEANFILES = $(man_MANS)

if MEDIA_ENABLED
haze_media_sources = media-backend.c \
                     media-backend.h \
                     media-channel.c \
                     media-channel.h \
                     media-manager.c \
                     media-manager.h \
                     media-stream.c \
                     media-stream.h
else
haze_media_sources =
endif

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
                         connection-capabilities.c \
                         connection-capabilities.h \
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
                         request.h \
                         util.c \
                         util.h \
                         $(haze_media_sources)

if ENABLE_MAIL_NOTIFICATION
telepathy_haze_SOURCES += \
                         connection-mail.h \
                         connection-mail.c
endif

telepathy_haze_LDADD = $(top_builddir)/extensions/libhaze-extensions.la

AM_CFLAGS = \
	-I$(top_srcdir) \
	-I$(top_builddir) \
	-DG_LOG_DOMAIN=\"haze\" \
	$(ERROR_CFLAGS) \
	@PURPLE_CFLAGS@ \
	@TP_GLIB_CFLAGS@ \
	@DBUS_GLIB_CFLAGS@ \
	@GLIB_CFLAGS@

AM_LDFLAGS = @PURPLE_LIBS@ @TP_GLIB_LIBS@ @DBUS_GLIB_LIBS@ @GLIB_LIBS@
