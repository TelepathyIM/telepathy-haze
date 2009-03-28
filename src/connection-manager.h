#ifndef __HAZE_CONNECTION_MANAGER_H__
#define __HAZE_CONNECTION_MANAGER_H__

/*
 * connection-manager.h - HazeConnectionManager header
 * Copyright (C) 2007 Will Thompson
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <glib-object.h>
#include <telepathy-glib/base-connection-manager.h>

#include "connection.h"

G_BEGIN_DECLS

typedef struct _HazeConnectionManager HazeConnectionManager;
typedef struct _HazeConnectionManagerClass HazeConnectionManagerClass;

struct _HazeConnectionManagerClass {
    TpBaseConnectionManagerClass parent_class;

    /** GHashTable of (gchar *)tp_protocol_name => HazeProtocolInfo */
    GHashTable *protocol_info_table;
};

struct _HazeConnectionManager {
    TpBaseConnectionManager parent;
};

typedef struct _HazeProtocolInfo HazeProtocolInfo;
struct _HazeProtocolInfo
{
    /** Not const for convenience, but should not be freed */
    gchar *tp_protocol_name;

    /** Not const for convenience, but should not be freed */
    gchar *prpl_id;
    PurplePluginProtocolInfo *prpl_info;

    /** A string of the form
     *      "foo:bar,baz:badger"
     *  where foo and baz are the names of prpl account options, and bar
     *  and badger are the names theses options should be given as
     *  Telepathy parameters.
     */
    const gchar *parameter_map;
};

GType haze_connection_manager_get_type (void);

/* TYPE MACROS */
#define HAZE_TYPE_CONNECTION_MANAGER \
  (haze_connection_manager_get_type ())
#define HAZE_CONNECTION_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), HAZE_TYPE_CONNECTION_MANAGER, \
                              HazeConnectionManager))
#define HAZE_CONNECTION_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), HAZE_TYPE_CONNECTION_MANAGER, \
                           HazeConnectionManagerClass))
#define HAZE_IS_CONNECTION_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), HAZE_TYPE_CONNECTION_MANAGER))
#define HAZE_IS_CONNECTION_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), HAZE_TYPE_CONNECTION_MANAGER))
#define HAZE_CONNECTION_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), HAZE_TYPE_CONNECTION_MANAGER, \
                              HazeConnectionManagerClass))

G_END_DECLS

#endif /* #ifndef __HAZE_CONNECTION_MANAGER_H__*/
