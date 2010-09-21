/*
 * media-stream.h - Header for HazeMediaStream
 * Copyright (C) 2006, 2009 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *
 * Copied heavily from telepathy-gabble.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __HAZE_MEDIA_STREAM_H__
#define __HAZE_MEDIA_STREAM_H__

#include <glib-object.h>
#include <libpurple/media.h>
#include <telepathy-glib/telepathy-glib.h>

G_BEGIN_DECLS

typedef enum
{
  STREAM_SIG_STATE_NEW,
  STREAM_SIG_STATE_SENT,
  STREAM_SIG_STATE_ACKNOWLEDGED,
  STREAM_SIG_STATE_REMOVING
} StreamSignallingState;

typedef guint32 CombinedStreamDirection;

typedef struct _HazeMediaStream HazeMediaStream;
typedef struct _HazeMediaStreamClass HazeMediaStreamClass;
typedef struct _HazeMediaStreamPrivate HazeMediaStreamPrivate;

struct _HazeMediaStreamClass {
    GObjectClass parent_class;

    TpDBusPropertiesMixinClass dbus_props_class;
};

struct _HazeMediaStream {
    GObject parent;

    gchar *name;
    gchar *peer;

    TpMediaStreamState connection_state;

    CombinedStreamDirection combined_direction;
    gboolean playing;

    HazeMediaStreamPrivate *priv;
};

GType haze_media_stream_get_type (void);

/* TYPE MACROS */
#define HAZE_TYPE_MEDIA_STREAM \
  (haze_media_stream_get_type ())
#define HAZE_MEDIA_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), HAZE_TYPE_MEDIA_STREAM, \
                              HazeMediaStream))
#define HAZE_MEDIA_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), HAZE_TYPE_MEDIA_STREAM, \
                           HazeMediaStreamClass))
#define HAZE_IS_MEDIA_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), HAZE_TYPE_MEDIA_STREAM))
#define HAZE_IS_MEDIA_STREAM_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), HAZE_TYPE_MEDIA_STREAM))
#define HAZE_MEDIA_STREAM_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), HAZE_TYPE_MEDIA_STREAM, \
                              HazeMediaStreamClass))

#define COMBINED_DIRECTION_GET_DIRECTION(d) \
    ((TpMediaStreamDirection) ((d) & TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL))
#define COMBINED_DIRECTION_GET_PENDING_SEND(d) \
    ((TpMediaStreamPendingSend) ((d) >> 2))
#define MAKE_COMBINED_DIRECTION(d, p) \
    ((CombinedStreamDirection) ((d) | ((p) << 2)))

gboolean haze_media_stream_error (HazeMediaStream *self, guint err_no,
    const gchar *message, GError **error);

void haze_media_stream_close (HazeMediaStream *close);
void haze_media_stream_hold (HazeMediaStream *stream, gboolean hold);
gboolean haze_media_stream_change_direction (HazeMediaStream *stream,
    guint requested_dir, GError **error);
void haze_media_stream_accept_pending_local_send (HazeMediaStream *stream);

HazeMediaStream *haze_media_stream_new (const gchar *object_path,
    TpDBusDaemon *dbus_daemon,
    PurpleMedia *media,
    const gchar *name,
    const gchar *peer,
    guint media_type,
    guint id,
    gboolean created_locally,
    const gchar *nat_traversal,
    const GPtrArray *relay_info,
    gboolean local_hold);
TpMediaStreamType haze_media_stream_get_media_type (HazeMediaStream *self);

GList *haze_media_stream_get_local_candidates (HazeMediaStream *self);
GList *haze_media_stream_get_codecs (HazeMediaStream *self);
void haze_media_stream_add_remote_candidates (HazeMediaStream *self,
                                              GList *remote_candidates);
void haze_media_stream_set_remote_codecs (HazeMediaStream *self,
                                          GList *remote_codecs);
void haze_media_stream_add_stun_server (HazeMediaStream *self,
                                        const gchar *stun_ip,
                                        guint stun_port);

G_END_DECLS

#endif /* #ifndef __HAZE_MEDIA_STREAM_H__*/
