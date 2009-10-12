/*
 * media-manager.h - Header for HazeMediaManager
 * Copyright (C) 2006, 2009 Collabora Ltd.
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

#ifndef __MEDIA_MANAGER_H__
#define __MEDIA_MANAGER_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _HazeMediaManager HazeMediaManager;
typedef struct _HazeMediaManagerClass HazeMediaManagerClass;
typedef struct _HazeMediaManagerPrivate HazeMediaManagerPrivate;

struct _HazeMediaManagerClass {
  GObjectClass parent_class;
};

struct _HazeMediaManager {
  GObject parent;

  HazeMediaManagerPrivate *priv;
};

GType haze_media_manager_get_type (void);

/* TYPE MACROS */
#define HAZE_TYPE_MEDIA_MANAGER \
  (haze_media_manager_get_type ())
#define HAZE_MEDIA_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), HAZE_TYPE_MEDIA_MANAGER,\
                              HazeMediaManager))
#define HAZE_MEDIA_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), HAZE_TYPE_MEDIA_MANAGER,\
                           HazeMediaManagerClass))
#define HAZE_IS_MEDIA_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), HAZE_TYPE_MEDIA_MANAGER))
#define HAZE_IS_MEDIA_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), HAZE_TYPE_MEDIA_MANAGER))
#define HAZE_MEDIA_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), HAZE_TYPE_MEDIA_MANAGER,\
                              HazeMediaManagerClass))

G_END_DECLS

#endif /* #ifndef __MEDIA_MANAGER_H__ */

