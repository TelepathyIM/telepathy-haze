/*
 * Haze Protocol object
 *
 * Copyright © 2007 Will Thompson
 * Copyright © 2007-2010 Collabora Ltd.
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

#include "protocol.h"

#include <telepathy-glib/telepathy-glib.h>

G_DEFINE_TYPE (HazeProtocol, haze_protocol, TP_TYPE_BASE_PROTOCOL)

static void
haze_protocol_init (HazeProtocol *self)
{
}

static const TpCMParamSpec *
haze_protocol_get_parameters (TpBaseProtocol *self)
{
  g_assert_not_reached ();
}

static TpBaseConnection *
haze_protocol_new_connection (TpBaseProtocol *self,
    GHashTable *asv,
    GError **error)
{
  g_assert_not_reached ();
}

static void
haze_protocol_class_init (HazeProtocolClass *cls)
{
  TpBaseProtocolClass *base_class = (TpBaseProtocolClass *) cls;

  base_class->get_parameters = haze_protocol_get_parameters;
  base_class->new_connection = haze_protocol_new_connection;
}
