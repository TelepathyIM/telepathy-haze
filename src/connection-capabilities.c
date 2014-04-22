/*
 * connection-capabilities.c - Capabilities interface implementation of HazeConnection
 * Copyright (C) 2005, 2006, 2008, 2009 Collabora Ltd.
 * Copyright (C) 2005, 2006, 2008 Nokia Corporation
 *
 * Copied heavily from telepathy-gabble
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
#include "config.h"

#include "connection-capabilities.h"

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "connection.h"
#include "debug.h"

static void
haze_connection_update_capabilities (TpSvcConnectionInterfaceContactCapabilities1 *iface,
                                     const GPtrArray *clients,
                                     GDBusMethodInvocation *context)
{
  HazeConnection *self = HAZE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  tp_svc_connection_interface_contact_capabilities1_return_from_update_capabilities (
      context);
}

gboolean
haze_connection_contact_capabilities_fill_contact_attributes (
    HazeConnection *self,
    const gchar *dbus_interface,
    TpHandle handle,
    GVariantDict *attributes)
{
  if (!tp_strdiff (dbus_interface,
        TP_IFACE_CONNECTION_INTERFACE_CONTACT_CAPABILITIES1))
    {
      /* TODO: Check for presence */
      g_variant_dict_insert_value (attributes,
          TP_TOKEN_CONNECTION_INTERFACE_CONTACT_CAPABILITIES1_CAPABILITIES,
          g_variant_new_parsed ("[({%s: <%s>, %s: <%u>}, [%s])]",
            /* Fixed properties */
            TP_PROP_CHANNEL_CHANNEL_TYPE, TP_IFACE_CHANNEL_TYPE_TEXT,
            TP_PROP_CHANNEL_TARGET_ENTITY_TYPE,
              (guint32) TP_ENTITY_TYPE_CONTACT,
            /* Allowed properties */
            TP_PROP_CHANNEL_TARGET_HANDLE));
      return TRUE;
    }

  return FALSE;
}

void
haze_connection_contact_capabilities_iface_init (gpointer g_iface,
                                                 gpointer iface_data)
{
  TpSvcConnectionInterfaceContactCapabilities1Class *klass = g_iface;

#define IMPLEMENT(x) \
    tp_svc_connection_interface_contact_capabilities1_implement_##x (\
    klass, haze_connection_##x)
  IMPLEMENT(update_capabilities);
#undef IMPLEMENT
}
