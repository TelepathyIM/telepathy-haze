#ifndef __HAZE_CONNECTION_PRESENCE_H__
#define __HAZE_CONNECTION_PRESENCE_H__
/*
 * connection-presence.h - Presence interface header of HazeConnection
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

#include "connection.h"

void haze_connection_presence_class_init (GObjectClass *object_class);
void haze_connection_presence_init (GObject *object);

void
haze_connection_presence_account_status_changed (PurpleAccount *account,
                                                 PurpleStatus *status);

#endif
