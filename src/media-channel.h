/*
 * media-channel.h - Header for HazeMediaChannel
 * Copyright (C) 2006, 2009 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *
 * Copied heavily from telepathy-gabble
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

#ifndef __HAZE_MEDIA_CHANNEL_H__
#define __HAZE_MEDIA_CHANNEL_H__

#include <glib-object.h>

#include <telepathy-glib/dbus-properties-mixin.h>
#include <telepathy-glib/group-mixin.h>

G_BEGIN_DECLS

typedef struct _HazeMediaChannel HazeMediaChannel;
typedef struct _HazeMediaChannelPrivate HazeMediaChannelPrivate;
typedef struct _HazeMediaChannelClass HazeMediaChannelClass;

struct _HazeMediaChannelClass {
    GObjectClass parent_class;

    TpGroupMixinClass group_class;
    TpDBusPropertiesMixinClass dbus_props_class;
};

struct _HazeMediaChannel {
    GObject parent;

    TpGroupMixin group;

    HazeMediaChannelPrivate *priv;
};

GType haze_media_channel_get_type (void);

/* TYPE MACROS */
#define HAZE_TYPE_MEDIA_CHANNEL \
  (haze_media_channel_get_type ())
#define HAZE_MEDIA_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), HAZE_TYPE_MEDIA_CHANNEL,\
                              HazeMediaChannel))
#define HAZE_MEDIA_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), HAZE_TYPE_MEDIA_CHANNEL,\
                           HazeMediaChannelClass))
#define HAZE_IS_MEDIA_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), HAZE_TYPE_MEDIA_CHANNEL))
#define HAZE_IS_MEDIA_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), HAZE_TYPE_MEDIA_CHANNEL))
#define HAZE_MEDIA_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), HAZE_TYPE_MEDIA_CHANNEL, \
                              HazeMediaChannelClass))

void haze_media_channel_request_initial_streams (HazeMediaChannel *chan,
    GFunc succeeded_cb,
    GFunc failed_cb,
    gpointer user_data);

void haze_media_channel_close (HazeMediaChannel *self);

G_END_DECLS

#endif /* #ifndef __HAZE_MEDIA_CHANNEL_H__*/
