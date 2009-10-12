/*
 * media-manager.c - Source for HazeMediaManager
 * Copyright (C) 2009 Collabora Ltd.
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

#include "config.h"
#include "media-manager.h"

#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>

#include "connection.h"
#include "debug.h"
#include "mediamanager.h"

static void channel_manager_iface_init (gpointer, gpointer);
static void haze_media_manager_close_all (HazeMediaManager *self);
static void haze_media_manager_constructed (GObject *object);

G_DEFINE_TYPE_WITH_CODE (HazeMediaManager, haze_media_manager,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      channel_manager_iface_init));

/* properties */
enum
{
  PROP_CONNECTION = 1,
  LAST_PROPERTY
};

struct _HazeMediaManagerPrivate
{
  HazeConnection *conn;
  gulong status_changed_id;

  GPtrArray *channels;
  guint channel_index;

  gboolean dispose_has_run;
};

static void
haze_media_manager_init (HazeMediaManager *self)
{
  HazeMediaManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      HAZE_TYPE_MEDIA_MANAGER, HazeMediaManagerPrivate);

  self->priv = priv;

  priv->channels = g_ptr_array_sized_new (1);
  priv->channel_index = 0;

  priv->conn = NULL;
  priv->dispose_has_run = FALSE;
}

static void
haze_media_manager_dispose (GObject *object)
{
  HazeMediaManager *self = HAZE_MEDIA_MANAGER (object);
  HazeMediaManagerPrivate *priv = self->priv;

  if (priv->dispose_has_run)
    return;

  DEBUG ("dispose called");
  priv->dispose_has_run = TRUE;

  haze_media_manager_close_all (self);
  g_assert (priv->channels->len == 0);
  g_ptr_array_free (priv->channels, TRUE);

  if (G_OBJECT_CLASS (haze_media_manager_parent_class)->dispose)
    G_OBJECT_CLASS (haze_media_manager_parent_class)->dispose (object);
}

static void
haze_media_manager_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  HazeMediaManager *self = HAZE_MEDIA_MANAGER (object);
  HazeMediaManagerPrivate *priv = self->priv;

  switch (property_id) {
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
haze_media_manager_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  HazeMediaManager *self = HAZE_MEDIA_MANAGER (object);
  HazeMediaManagerPrivate *priv = self->priv;

  switch (property_id) {
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
haze_media_manager_class_init (HazeMediaManagerClass *haze_media_manager_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (haze_media_manager_class);
  GParamSpec *param_spec;

  g_type_class_add_private (haze_media_manager_class,
      sizeof (HazeMediaManagerPrivate));

  object_class->constructed = haze_media_manager_constructed;
  object_class->dispose = haze_media_manager_dispose;

  object_class->get_property = haze_media_manager_get_property;
  object_class->set_property = haze_media_manager_set_property;

  param_spec = g_param_spec_object ("connection", "HazeConnection object",
      "Haze connection object that owns this media channel manager object.",
      HAZE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

}

static void
haze_media_manager_close_all (HazeMediaManager *self)
{
  HazeMediaManagerPrivate *priv = self->priv;
  /* TODO: uncomment this when there's a channel class */
#if 0
  GPtrArray *ret = g_ptr_array_sized_new (priv->channels->len);
  guint i;

  for (i = 0; i < priv->channels->len; i++)
    g_ptr_array_add (ret, g_ptr_array_index (priv->channels, i));

  DEBUG ("closing channels");

  for (i = 0; i < tmp->len; i++)
    {
      HazeMediaChannel *chan = g_ptr_array_index (tmp, i);

      DEBUG ("closing %p", chan);
      haze_media_channel_close (chan);
    }
#endif
  if (priv->status_changed_id != 0)
    {
      g_signal_handler_disconnect (priv->conn,
          priv->status_changed_id);
      priv->status_changed_id = 0;
    }
}

static gboolean
init_media_cb (PurpleMediaManager *manager,
               PurpleMedia *media,
               PurpleAccount *account,
               const gchar *username,
               HazeMediaManager *self)
{
  /* TODO: flesh out this function */
  DEBUG ("init_media");
  return FALSE;
}

static void
connection_status_changed_cb (HazeConnection *conn,
                              guint status,
                              guint reason,
                              HazeMediaManager *self)
{
  switch (status)
    {
    case TP_CONNECTION_STATUS_CONNECTING:
      g_signal_connect (purple_media_manager_get (), "init-media",
          G_CALLBACK (init_media_cb), self);
      break;

    case TP_CONNECTION_STATUS_DISCONNECTED:
      g_signal_handlers_disconnect_by_func (purple_media_manager_get (),
          G_CALLBACK (init_media_cb), self);
      haze_media_manager_close_all (self);
      break;
    }
}

static void
haze_media_manager_constructed (GObject *object)
{
  void (*chain_up) (GObject *) =
      G_OBJECT_CLASS (haze_media_manager_parent_class)->constructed;
  HazeMediaManager *self = HAZE_MEDIA_MANAGER (object);
  HazeMediaManagerPrivate *priv = self->priv;

  if (chain_up != NULL)
    chain_up (object);

  priv->status_changed_id = g_signal_connect (priv->conn,
      "status-changed", (GCallback) connection_status_changed_cb, object);
}

static void
haze_media_manager_foreach_channel (TpChannelManager *manager,
                                    TpExportableChannelFunc foreach,
                                    gpointer user_data)
{
  HazeMediaManager *self = HAZE_MEDIA_MANAGER (manager);
  HazeMediaManagerPrivate *priv = self->priv;
  guint i;

  for (i = 0; i < priv->channels->len; i++)
    {
      TpExportableChannel *channel = TP_EXPORTABLE_CHANNEL (
          g_ptr_array_index (priv->channels, i));

      foreach (channel, user_data);
    }
}

static const gchar * const named_channel_allowed_properties[] = {
    TP_IFACE_CHANNEL ".TargetHandle",
    TP_IFACE_CHANNEL ".TargetID",
    TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA ".InitialAudio",
    TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA ".InitialVideo",
    NULL
};

static GHashTable *
haze_media_manager_channel_class (void)
{
  return tp_asv_new (
      TP_IFACE_CHANNEL ".ChannelType", G_TYPE_STRING,
          TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
      TP_IFACE_CHANNEL ".TargetHandleType", G_TYPE_UINT,
          TP_HANDLE_TYPE_CONTACT,
      NULL);
}

static void
haze_media_manager_foreach_channel_class (TpChannelManager *manager,
    TpChannelManagerChannelClassFunc func,
    gpointer user_data)
{
  GHashTable *table = haze_media_manager_channel_class ();

  func (manager, table, named_channel_allowed_properties, user_data);

  g_hash_table_destroy (table);
}

typedef enum
{
  METHOD_REQUEST,
  METHOD_CREATE,
  METHOD_ENSURE,
} RequestMethod;

static gboolean
haze_media_manager_requestotron (TpChannelManager *manager,
                                 gpointer request_token,
                                 GHashTable *request_properties,
                                 RequestMethod method)
{
  /* TODO: Implement the rest of this once there's a channel class */
  if (tp_strdiff (tp_asv_get_string (request_properties,
    TP_IFACE_CHANNEL ".ChannelType"),
      TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA))
    return FALSE;

  return FALSE;
}

static gboolean
haze_media_manager_request_channel (TpChannelManager *manager,
                                    gpointer request_token,
                                    GHashTable *request_properties)
{
  return haze_media_manager_requestotron (manager, request_token,
      request_properties, METHOD_REQUEST);
}


static gboolean
haze_media_manager_create_channel (TpChannelManager *manager,
                                   gpointer request_token,
                                   GHashTable *request_properties)
{
  return haze_media_manager_requestotron (manager, request_token,
      request_properties, METHOD_CREATE);
}

static gboolean
haze_media_manager_ensure_channel (TpChannelManager *manager,
                                   gpointer request_token,
                                   GHashTable *request_properties)
{
  return haze_media_manager_requestotron (manager, request_token,
      request_properties, METHOD_ENSURE);
}

static void
channel_manager_iface_init (gpointer g_iface,
                            gpointer iface_data)
{
  TpChannelManagerIface *iface = g_iface;

  iface->foreach_channel = haze_media_manager_foreach_channel;
  iface->foreach_channel_class = haze_media_manager_foreach_channel_class;
  iface->request_channel = haze_media_manager_request_channel;
  iface->create_channel = haze_media_manager_create_channel;
  iface->ensure_channel = haze_media_manager_ensure_channel;
}
