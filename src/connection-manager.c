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

static void *
_haze_cm_alloc_params (void)
{
    /* (gchar *) => (GValue *) */
    return g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
        (GDestroyNotify) tp_g_value_slice_free);
}

static void
_haze_cm_set_param (const TpCMParamSpec *paramspec,
                    const GValue *value,
                    gpointer params)
{
  /* just pass straight through - HazeProtocol does the real work */
  g_hash_table_insert (params, (gchar *) paramspec->name,
      tp_g_value_slice_dup (value));
}

static int
_compare_protocol_names (gconstpointer a,
                         gconstpointer b)
{
  const TpCMProtocolSpec *protocol_a = a;
  const TpCMProtocolSpec *protocol_b = b;

  return strcmp(protocol_a->name, protocol_b->name);
}

static TpCMProtocolSpec *
get_protocols (HazeConnectionManagerClass *klass)
{
  GArray *protocols = g_array_new (TRUE, TRUE, sizeof (TpCMProtocolSpec));
  GList *iter;

  for (iter = klass->protocols; iter != NULL; iter = iter->next)
    {
      HazeProtocol *obj = iter->data;
      TpCMProtocolSpec protocol = {
          NULL,
          tp_base_protocol_get_parameters ((TpBaseProtocol *) obj),
          _haze_cm_alloc_params, /* params_new */
          (GDestroyNotify) g_hash_table_unref, /* params_free */
          _haze_cm_set_param /* set_param */
      };

      /* leaked once per process */
      g_object_get (obj,
          "name", &protocol.name,
          NULL);

      g_array_append_val (protocols, protocol);
    }

  qsort (protocols->data, protocols->len, sizeof (TpCMProtocolSpec),
      _compare_protocol_names);

  {
    GString *debug_string = g_string_new ("");
    TpCMProtocolSpec *p = (TpCMProtocolSpec *) protocols->data;

    while (p->name != NULL)
      {
        g_string_append (debug_string, p->name);
        p += 1;

        if (p->name != NULL)
          g_string_append (debug_string, ", ");
      }

    DEBUG ("Found protocols %s", debug_string->str);
    g_string_free (debug_string, TRUE);
  }

  return (TpCMProtocolSpec *) g_array_free (protocols, FALSE);
}

static TpBaseConnection *
_haze_connection_manager_new_connection (TpBaseConnectionManager *base,
                                         const gchar *proto,
                                         TpIntSet *params_present,
                                         void *parsed_params,
                                         GError **error)
{
    HazeConnectionManagerClass *klass = HAZE_CONNECTION_MANAGER_GET_CLASS (base);
    GList *iter;

    for (iter = klass->protocols; iter != NULL; iter = iter->next)
      {
        gchar *name;

        g_object_get (iter->data,
            "name", &name,
            NULL);

        if (!tp_strdiff (proto, name))
          {
            g_free (name);
            return tp_base_protocol_new_connection (iter->data, parsed_params,
                error);
          }

        g_free (name);
      }

    /* telepathy-glib should stop us getting here */
    g_assert_not_reached ();
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

    klass->protocol_info_table = haze_protocol_build_protocol_table (
        &klass->protocols);

    object_class->finalize = _haze_cm_finalize;

    base_class->new_connection = _haze_connection_manager_new_connection;
    base_class->cm_dbus_name = "haze";
    base_class->protocol_params = get_protocols (klass);

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
