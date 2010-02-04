/*
 * media-backend.h - Header for HazeMediaBackend
 * Copyright (C) 2006, 2009 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
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

#ifndef __HAZE_MEDIA_BACKEND__
#define __HAZE_MEDIA_BACKEND__

#include <glib-object.h>

#include "media-stream.h"

G_BEGIN_DECLS

typedef struct _HazeMediaBackend HazeMediaBackend;
typedef struct _HazeMediaBackendClass HazeMediaBackendClass;
typedef struct _HazeMediaBackendPrivate HazeMediaBackendPrivate;

struct _HazeMediaBackendClass {
    GObjectClass parent_class;
};

struct _HazeMediaBackend {
    GObject parent;

    HazeMediaBackendPrivate *priv;
};

GType haze_media_backend_get_type (void);
HazeMediaStream *haze_media_backend_get_stream_by_name (
    HazeMediaBackend *self,
    const gchar *sid);

/* TYPE MACROS */
#define HAZE_TYPE_MEDIA_BACKEND \
  (haze_media_backend_get_type ())
#define HAZE_MEDIA_BACKEND(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), HAZE_TYPE_MEDIA_BACKEND, \
                              HazeMediaBackend))
#define HAZE_MEDIA_BACKEND_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), HAZE_TYPE_MEDIA_BACKEND, \
                           HazeMediaBackendClass))
#define HAZE_IS_MEDIA_BACKEND(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), HAZE_TYPE_MEDIA_BACKEND))
#define HAZE_IS_MEDIA_BACKEND_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), HAZE_TYPE_MEDIA_BACKEND))
#define HAZE_MEDIA_BACKEND_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), HAZE_TYPE_MEDIA_BACKEND, \
                              HazeMediaBackendClass))

G_END_DECLS

#endif /* #ifndef __HAZE_MEDIA_BACKEND__ */
