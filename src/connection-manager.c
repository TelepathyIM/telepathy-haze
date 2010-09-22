/*
 * connection-manager.c - HazeConnectionManager source
 * Copyright (C) 2007 Will Thompson
 * Copyright (C) 2007-2009 Collabora Ltd.
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

#include <string.h>

#include <glib.h>
#include <dbus/dbus-protocol.h>

#include <libpurple/prpl.h>
#include <libpurple/accountopt.h>

#include <telepathy-glib/debug-sender.h>

#include "connection-manager.h"
#include "debug.h"

G_DEFINE_TYPE(HazeConnectionManager,
    haze_connection_manager,
    TP_TYPE_BASE_CONNECTION_MANAGER)

typedef struct _HazeConnectionManagerPrivate HazeConnectionManagerPrivate;
struct _HazeConnectionManagerPrivate
{
    TpDebugSender *debug_sender;
};

static void
_haze_cm_constructed (GObject *object)
{
  HazeConnectionManager *self = HAZE_CONNECTION_MANAGER (object);
  TpBaseConnectionManager *base = (TpBaseConnectionManager *) self;
  void (*chain_up) (GObject *) =
    G_OBJECT_CLASS (haze_connection_manager_parent_class)->constructed;
  GList *protocols;

  if (chain_up != NULL)
    {
      chain_up (object);
    }

  for (protocols = haze_protocol_build_list ();
      protocols != NULL;
      protocols = g_list_delete_link (protocols, protocols))
    {
      tp_base_connection_manager_add_protocol (base, protocols->data);
      g_object_unref (protocols->data);
    }
}

static void
_haze_cm_finalize (GObject *object)
{
    HazeConnectionManager *self = HAZE_CONNECTION_MANAGER (object);
    void (*chain_up) (GObject *) =
      G_OBJECT_CLASS (haze_connection_manager_parent_class)->finalize;
    HazeConnectionManagerPrivate *priv = self->priv;

    if (priv->debug_sender != NULL)
    {
        g_object_unref (priv->debug_sender);
        priv->debug_sender = NULL;
    }

    if (chain_up != NULL)
    {
        chain_up (object);
    }
}

static void
haze_connection_manager_class_init (HazeConnectionManagerClass *klass)
{
    TpBaseConnectionManagerClass *base_class =
        (TpBaseConnectionManagerClass *)klass;
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->constructed = _haze_cm_constructed;
    object_class->finalize = _haze_cm_finalize;

    base_class->new_connection = NULL;
    base_class->cm_dbus_name = "haze";
    base_class->protocol_params = NULL;

    g_type_class_add_private (klass, sizeof (HazeConnectionManagerPrivate));
}

static void
haze_connection_manager_init (HazeConnectionManager *self)
{
    HazeConnectionManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
        HAZE_TYPE_CONNECTION_MANAGER, HazeConnectionManagerPrivate);

    self->priv = priv;

    priv->debug_sender = tp_debug_sender_dup ();
    g_log_set_default_handler (tp_debug_sender_log_handler, G_LOG_DOMAIN);

    DEBUG ("Initializing (HazeConnectionManager *)%p", self);
}
