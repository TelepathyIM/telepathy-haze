/*
 * media-stream.c - Source for HazeMediaStream
 * Copyright © 2006-2009 Collabora Ltd.
 * Copyright © 2006-2009 Nokia Corporation
 *
 * Copied heavily from telepathy-gabble.
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
#include "media-stream.h"

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/properties-mixin.h>

#define DEBUG_FLAG HAZE_DEBUG_MEDIA

#include "debug.h"

static void dbus_properties_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (HazeMediaStream,
    haze_media_stream,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
      dbus_properties_iface_init)
    )

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_NAME,
  PROP_ID,
  PROP_MEDIA_TYPE,
  PROP_CONNECTION_STATE,
  PROP_READY,
  PROP_PLAYING,
  PROP_COMBINED_DIRECTION,
  PROP_LOCAL_HOLD,
  PROP_MEDIA,
  PROP_STUN_SERVERS,
  PROP_RELAY_INFO,
  PROP_NAT_TRAVERSAL,
  PROP_CREATED_LOCALLY,
  LAST_PROPERTY
};

/* private structure */

struct _HazeMediaStreamPrivate
{
  PurpleMedia *media;

  gchar *object_path;
  guint id;
  guint media_type;

  GValue native_codecs;     /* intersected codec list */

  /* Whether we're waiting for a codec intersection from the streaming
   * implementation. If FALSE, SupportedCodecs is a no-op.
   */
  gboolean awaiting_intersection;

  GValue remote_codecs;
  GValue remote_candidates;

  guint remote_candidate_count;

  /* source ID for initial codecs/candidates getter */
  gulong initial_getter_id;

  gchar *nat_traversal;
  /* GPtrArray(GValueArray(STRING, UINT)) */
  GPtrArray *stun_servers;
  /* GPtrArray(GHashTable(string => GValue)) */
  GPtrArray *relay_info;

  gboolean on_hold;

  /* These are really booleans, but gboolean is signed. Thanks, GLib */
  unsigned closed:1;
  unsigned dispose_has_run:1;
  unsigned local_hold:1;
  unsigned ready:1;
  unsigned sending:1;
  unsigned created_locally:1;
};

HazeMediaStream *
haze_media_stream_new (const gchar *object_path,
    PurpleMedia *media,
    const gchar *name,
    guint id,
    const gchar *nat_traversal,
    const GPtrArray *relay_info,
    gboolean local_hold)
{
  GPtrArray *empty = NULL;
  HazeMediaStream *result;

  g_return_val_if_fail (PURPLE_IS_MEDIA (media), NULL);

  if (relay_info == NULL)
    {
      empty = g_ptr_array_sized_new (0);
      relay_info = empty;
    }

  result = g_object_new (HAZE_TYPE_MEDIA_STREAM,
      "object-path", object_path,
      "media", media,
      "name", name,
      "id", id,
      "nat-traversal", nat_traversal,
      "relay-info", relay_info,
      "local-hold", local_hold,
      NULL);

  if (empty != NULL)
    g_ptr_array_free (empty, TRUE);

  return result;
}

TpMediaStreamType
haze_media_stream_get_media_type (HazeMediaStream *self)
{
  return self->priv->media_type;
}

static void
haze_media_stream_init (HazeMediaStream *self)
{
  HazeMediaStreamPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      HAZE_TYPE_MEDIA_STREAM, HazeMediaStreamPrivate);
  GType candidate_list_type =
      TP_ARRAY_TYPE_MEDIA_STREAM_HANDLER_CANDIDATE_LIST;
  GType codec_list_type =
      TP_ARRAY_TYPE_MEDIA_STREAM_HANDLER_CODEC_LIST;

  self->priv = priv;

  g_value_init (&priv->native_codecs, codec_list_type);
  g_value_take_boxed (&priv->native_codecs,
      dbus_g_type_specialized_construct (codec_list_type));

  g_value_init (&priv->remote_codecs, codec_list_type);
  g_value_take_boxed (&priv->remote_codecs,
      dbus_g_type_specialized_construct (codec_list_type));

  g_value_init (&priv->remote_candidates, candidate_list_type);
  g_value_take_boxed (&priv->remote_candidates,
      dbus_g_type_specialized_construct (candidate_list_type));

  priv->stun_servers = g_ptr_array_sized_new (1);
}

static GObject *
haze_media_stream_constructor (GType type, guint n_props,
                               GObjectConstructParam *props)
{
  GObject *obj;
  HazeMediaStream *stream;
  HazeMediaStreamPrivate *priv;
  DBusGConnection *bus;

  /* call base class constructor */
  obj = G_OBJECT_CLASS (haze_media_stream_parent_class)->
           constructor (type, n_props, props);
  stream = HAZE_MEDIA_STREAM (obj);
  priv = stream->priv;

  g_assert (priv->media != NULL);

  /* go for the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  return obj;
}

static void
haze_media_stream_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  HazeMediaStream *stream = HAZE_MEDIA_STREAM (object);
  HazeMediaStreamPrivate *priv = stream->priv;

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_NAME:
      g_value_set_string (value, stream->name);
      break;
    case PROP_ID:
      g_value_set_uint (value, priv->id);
      break;
    case PROP_MEDIA_TYPE:
      g_value_set_uint (value, priv->media_type);
      break;
    case PROP_CONNECTION_STATE:
      g_value_set_uint (value, stream->connection_state);
      break;
    case PROP_READY:
      g_value_set_boolean (value, priv->ready);
      break;
    case PROP_PLAYING:
      g_value_set_boolean (value, stream->playing);
      break;
    case PROP_COMBINED_DIRECTION:
      g_value_set_uint (value, stream->combined_direction);
      break;
    case PROP_LOCAL_HOLD:
      g_value_set_boolean (value, priv->local_hold);
      break;
    case PROP_MEDIA:
      g_value_set_object (value, priv->media);
      break;
    case PROP_STUN_SERVERS:
      g_value_set_boxed (value, priv->stun_servers);
      break;
    case PROP_NAT_TRAVERSAL:
      g_value_set_string (value, priv->nat_traversal);
      break;
    case PROP_CREATED_LOCALLY:
      g_value_set_boolean (value, priv->created_locally);
      break;
    case PROP_RELAY_INFO:
      g_value_set_boxed (value, priv->relay_info);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
haze_media_stream_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  HazeMediaStream *stream = HAZE_MEDIA_STREAM (object);
  HazeMediaStreamPrivate *priv = stream->priv;

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_NAME:
      g_free (stream->name);
      stream->name = g_value_dup_string (value);
      break;
    case PROP_ID:
      priv->id = g_value_get_uint (value);
      break;
    case PROP_CONNECTION_STATE:
      DEBUG ("stream %s connection state %d",
          stream->name, stream->connection_state);
      stream->connection_state = g_value_get_uint (value);
      break;
    case PROP_READY:
      priv->ready = g_value_get_boolean (value);
      break;
    case PROP_PLAYING:
      break;
    case PROP_COMBINED_DIRECTION:
      DEBUG ("changing combined direction from %u to %u",
          stream->combined_direction, g_value_get_uint (value));
      stream->combined_direction = g_value_get_uint (value);
      break;
    case PROP_MEDIA:
      g_assert (priv->media == NULL);
      priv->media = g_value_dup_object (value);
    case PROP_NAT_TRAVERSAL:
      g_assert (priv->nat_traversal == NULL);
      priv->nat_traversal = g_value_dup_string (value);
      break;
    case PROP_RELAY_INFO:
      g_assert (priv->relay_info == NULL);
      priv->relay_info = g_value_dup_boxed (value);
      break;
    case PROP_LOCAL_HOLD:
      priv->local_hold = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void haze_media_stream_dispose (GObject *object);
static void haze_media_stream_finalize (GObject *object);

static void
haze_media_stream_class_init (HazeMediaStreamClass *haze_media_stream_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (haze_media_stream_class);
  GParamSpec *param_spec;

  g_type_class_add_private (haze_media_stream_class,
      sizeof (HazeMediaStreamPrivate));

  object_class->constructor = haze_media_stream_constructor;

  object_class->get_property = haze_media_stream_get_property;
  object_class->set_property = haze_media_stream_set_property;

  object_class->dispose = haze_media_stream_dispose;
  object_class->finalize = haze_media_stream_finalize;

  param_spec = g_param_spec_string ("object-path", "D-Bus object path",
                                    "The D-Bus object path used for this "
                                    "object on the bus.",
                                    NULL,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NAME |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_string ("name", "Stream name",
      "An opaque name for the stream used in the signalling.", NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_NAME, param_spec);

  param_spec = g_param_spec_uint ("id", "Stream ID",
                                  "A stream number for the stream used in the "
                                  "D-Bus API.",
                                  0, G_MAXUINT, 0,
                                  G_PARAM_CONSTRUCT_ONLY |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_ID, param_spec);

  param_spec = g_param_spec_uint ("media-type", "Stream media type",
      "A constant indicating which media type the stream carries.",
      TP_MEDIA_STREAM_TYPE_AUDIO, TP_MEDIA_STREAM_TYPE_VIDEO,
      TP_MEDIA_STREAM_TYPE_AUDIO,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MEDIA_TYPE, param_spec);

  param_spec = g_param_spec_uint ("connection-state", "Stream connection state",
                                  "An integer indicating the state of the"
                                  "stream's connection.",
                                  TP_MEDIA_STREAM_STATE_DISCONNECTED,
                                  TP_MEDIA_STREAM_STATE_CONNECTED,
                                  TP_MEDIA_STREAM_STATE_DISCONNECTED,
                                  G_PARAM_CONSTRUCT |
                                  G_PARAM_READWRITE |
                                  G_PARAM_STATIC_NAME |
                                  G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_CONNECTION_STATE,
      param_spec);

  param_spec = g_param_spec_boolean ("ready", "Ready?",
                                     "A boolean signifying whether the user "
                                     "is ready to handle signals from this "
                                     "object.",
                                     FALSE,
                                     G_PARAM_CONSTRUCT |
                                     G_PARAM_READWRITE |
                                     G_PARAM_STATIC_NAME |
                                     G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_READY, param_spec);

  param_spec = g_param_spec_boolean ("playing", "Set playing",
                                     "A boolean signifying whether the stream "
                                     "has been set playing yet.",
                                     FALSE,
                                     G_PARAM_CONSTRUCT |
                                     G_PARAM_READWRITE |
                                     G_PARAM_STATIC_NAME |
                                     G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PLAYING, param_spec);

  param_spec = g_param_spec_uint ("combined-direction",
      "Combined direction",
      "An integer indicating the directions the stream currently sends in, "
      "and the peers who have been asked to send.",
      TP_MEDIA_STREAM_DIRECTION_NONE,
      MAKE_COMBINED_DIRECTION (TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL,
        TP_MEDIA_STREAM_PENDING_LOCAL_SEND |
        TP_MEDIA_STREAM_PENDING_REMOTE_SEND),
      TP_MEDIA_STREAM_DIRECTION_NONE,
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_COMBINED_DIRECTION,
      param_spec);

  param_spec = g_param_spec_boolean ("local-hold", "Local hold?",
      "True if resources used for this stream have been freed.", FALSE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY |
      G_PARAM_STATIC_NAME | G_PARAM_STATIC_BLURB | G_PARAM_STATIC_NICK);
  g_object_class_install_property (object_class, PROP_LOCAL_HOLD, param_spec);

  param_spec = g_param_spec_object ("media", "PurpleMedia object",
                                    "Media object signalling this media stream.",
                                    PURPLE_TYPE_MEDIA,
                                    G_PARAM_CONSTRUCT_ONLY |
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_NICK |
                                    G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_MEDIA, param_spec);

  param_spec = g_param_spec_boxed ("stun-servers", "STUN servers",
      "Array of (STRING: address literal, UINT: port) pairs",
      /* FIXME: use correct macro when available */
      tp_type_dbus_array_su (),
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_STUN_SERVERS, param_spec);

  param_spec = g_param_spec_boxed ("relay-info", "Relay info",
      "Array of mappings containing relay server information",
      TP_ARRAY_TYPE_STRING_VARIANT_MAP_LIST,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_RELAY_INFO, param_spec);

  param_spec = g_param_spec_string ("nat-traversal", "NAT traversal",
      "NAT traversal mechanism for this stream", NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_NAT_TRAVERSAL,
      param_spec);

  param_spec = g_param_spec_boolean ("created-locally", "Created locally?",
      "True if this stream was created by the local user", FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CREATED_LOCALLY,
      param_spec);
}

void
haze_media_stream_dispose (GObject *object)
{
  HazeMediaStream *self = HAZE_MEDIA_STREAM (object);
  HazeMediaStreamPrivate *priv = self->priv;

  DEBUG ("called");

  if (priv->dispose_has_run)
    return;

  if (priv->initial_getter_id != 0)
    {
      g_source_remove (priv->initial_getter_id);
      priv->initial_getter_id = 0;
    }

  priv->dispose_has_run = TRUE;

  g_object_unref (priv->media);
  priv->media = NULL;

  if (G_OBJECT_CLASS (haze_media_stream_parent_class)->dispose)
    G_OBJECT_CLASS (haze_media_stream_parent_class)->dispose (object);
}

void
haze_media_stream_finalize (GObject *object)
{
  HazeMediaStream *self = HAZE_MEDIA_STREAM (object);
  HazeMediaStreamPrivate *priv = self->priv;

  g_free (priv->object_path);
  g_free (priv->nat_traversal);

  /* FIXME: use correct macro when available */
  if (priv->stun_servers != NULL)
    g_boxed_free (tp_type_dbus_array_su (), priv->stun_servers);

  if (priv->relay_info != NULL)
    g_boxed_free (TP_ARRAY_TYPE_STRING_VARIANT_MAP_LIST, priv->relay_info);

  g_value_unset (&priv->native_codecs);

  g_value_unset (&priv->remote_codecs);
  g_value_unset (&priv->remote_candidates);

  G_OBJECT_CLASS (haze_media_stream_parent_class)->finalize (object);
}

static void
haze_media_stream_props_get (TpSvcDBusProperties *iface,
                             const gchar *interface_name,
                             const gchar *property_name,
                             DBusGMethodInvocation *context)
{
  HazeMediaStream *self = HAZE_MEDIA_STREAM (iface);
  GValue value = { 0 };

  if (!tp_strdiff (interface_name, TP_IFACE_MEDIA_STREAM_HANDLER))
    {
      if (!tp_strdiff (property_name, "RelayInfo"))
        {
          g_value_init (&value, TP_ARRAY_TYPE_STRING_VARIANT_MAP_LIST);
          g_object_get_property ((GObject *) self, "relay-info", &value);
        }
      else if (!tp_strdiff (property_name, "STUNServers"))
        {
          /* FIXME: use correct macro when available */
          g_value_init (&value, tp_type_dbus_array_su ());
          g_object_get_property ((GObject *) self, "stun-servers", &value);
        }
      else if (!tp_strdiff (property_name, "NATTraversal"))
        {
          g_value_init (&value, G_TYPE_STRING);
          g_object_get_property ((GObject *) self, "nat-traversal", &value);
        }
      else if (!tp_strdiff (property_name, "CreatedLocally"))
        {
          g_value_init (&value, G_TYPE_BOOLEAN);
          g_object_get_property ((GObject *) self, "created-locally", &value);
        }
      else
        {
          GError not_implemented = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
              "Property not implemented" };

          dbus_g_method_return_error (context, &not_implemented);
          return;
        }
    }
  else
    {
      GError not_implemented = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "Interface not implemented" };

      dbus_g_method_return_error (context, &not_implemented);
      return;
    }

  tp_svc_dbus_properties_return_from_get (context, &value);
  g_value_unset (&value);
}

static void
haze_media_stream_props_get_all (TpSvcDBusProperties *iface,
                                 const gchar *interface_name,
                                 DBusGMethodInvocation *context)
{
  HazeMediaStream *self = HAZE_MEDIA_STREAM (iface);

  if (!tp_strdiff (interface_name, TP_IFACE_MEDIA_STREAM_HANDLER))
    {
      GValue *value;
      GHashTable *values = g_hash_table_new_full (g_str_hash, g_str_equal,
          NULL, (GDestroyNotify) tp_g_value_slice_free);

      value = tp_g_value_slice_new (TP_ARRAY_TYPE_STRING_VARIANT_MAP_LIST);
      g_object_get_property ((GObject *) self, "relay-info", value);
      g_hash_table_insert (values, "RelayInfo", value);

      /* FIXME: use correct macro when available */
      value = tp_g_value_slice_new (tp_type_dbus_array_su ());
      g_object_get_property ((GObject *) self, "stun-servers", value);
      g_hash_table_insert (values, "STUNServers", value);

      value = tp_g_value_slice_new (G_TYPE_STRING);
      g_object_get_property ((GObject *) self, "nat-traversal", value);
      g_hash_table_insert (values, "NATTraversal", value);

      value = tp_g_value_slice_new (G_TYPE_BOOLEAN);
      g_object_get_property ((GObject *) self, "created-locally", value);
      g_hash_table_insert (values, "CreatedLocally", value);

      tp_svc_dbus_properties_return_from_get_all (context, values);
      g_hash_table_destroy (values);
    }
  else
    {
      GError not_implemented = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "Interface not implemented" };

      dbus_g_method_return_error (context, &not_implemented);
    }
}

static void
dbus_properties_iface_init (gpointer g_iface,
                            gpointer iface_data G_GNUC_UNUSED)
{
  TpSvcDBusPropertiesClass *cls = g_iface;

#define IMPLEMENT(x) \
    tp_svc_dbus_properties_implement_##x (cls, haze_media_stream_props_##x)
  IMPLEMENT (get);
  IMPLEMENT (get_all);
  /* set not implemented in this class */
#undef IMPLEMENT
}
