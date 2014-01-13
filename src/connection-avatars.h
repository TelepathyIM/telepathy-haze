#ifndef __HAZE_CONNECTION_AVATARS_H__
#define __HAZE_CONNECTION_AVATARS_H__
/*
 * connection-avatars.h - Avatars interface headers of HazeConnection
 * Copyright (C) 2007 Will Thompson
 * Copyright (C) 2007-2008 Collabora Ltd.
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
#include <telepathy-glib/telepathy-glib.h>

#include <libpurple/purple.h>

#include "connection.h"

void haze_connection_avatars_iface_init (gpointer g_iface, gpointer iface_data);
void haze_connection_avatars_class_init (GObjectClass *object_class);

extern TpDBusPropertiesMixinPropImpl *haze_connection_avatars_properties;
void haze_connection_avatars_properties_getter (GObject *object,
    GQuark interface, GQuark name, GValue *value, gpointer getter_data);

void haze_connection_get_icon_spec_requirements (PurpleBuddyIconSpec *icon_spec,
    GStrv *mime_types,
    guint *min_height,
    guint *min_width,
    guint *rec_height,
    guint *rec_width,
    guint *max_height,
    guint *max_width,
    guint *max_bytes);

gboolean haze_connection_avatars_fill_contact_attributes (
    HazeConnection *self,
    const gchar *dbus_interface,
    TpHandle handle,
    TpContactAttributeMap *attributes);

#endif
