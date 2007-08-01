bin_PROGRAMS = telepathy-haze

telepathy_haze_SOURCES = main.c \
                         defines.h \
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
                         im-channel-factory.h

AM_CFLAGS = @PURPLE_CFLAGS@ @TP_GLIB_CFLAGS@ -Wall -Werror
AM_LDFLAGS = @PURPLE_LIBS@ @TP_GLIB_LIBS@
