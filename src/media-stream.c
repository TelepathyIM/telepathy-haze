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

#include <libpurple/media/backend-iface.h>
#include <string.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/properties-mixin.h>
#include <telepathy-glib/svc-media-interfaces.h>

#define DEBUG_FLAG HAZE_DEBUG_MEDIA

#include "debug.h"

static void stream_handler_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE (HazeMediaStream,
    haze_media_stream,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
      tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_MEDIA_STREAM_HANDLER,
      stream_handler_iface_init)
    )

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_NAME,
  PROP_PEER,
  PROP_ID,
  PROP_MEDIA_TYPE,
  PROP_CONNECTION_STATE,
  PROP_READY,
  PROP_PLAYING,
  PROP_COMBINED_DIRECTION,
  PROP_LOCAL_HOLD,
  PROP_MEDIA,
  PROP_CODECS_READY,
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

  GList *codecs;
  GList *remote_codecs;
  GList *local_candidates;
  GList *remote_candidates;

  /* Whether we're waiting for a codec intersection from the streaming
   * implementation. If FALSE, SupportedCodecs is a no-op.
   */
  gboolean awaiting_intersection;

  guint remote_candidate_count;

  gchar *nat_traversal;
  /* GPtrArray(GValueArray(STRING, UINT)) */
  GPtrArray *stun_servers;
  /* GPtrArray(GHashTable(string => GValue)) */
  GPtrArray *relay_info;

  gboolean on_hold;

  gboolean closed;
  gboolean dispose_has_run;
  gboolean local_hold;
  gboolean ready;
  gboolean sending;
  gboolean created_locally;
};

HazeMediaStream *
haze_media_stream_new (const gchar *object_path,
    PurpleMedia *media,
    const gchar *name,
    const gchar *peer,
    guint media_type,
    guint id,
    gboolean created_locally,
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
      "peer", peer,
      "media-type", media_type,
      "id", id,
      "created-locally", created_locally,
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

  self->priv = priv;

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

  if (priv->created_locally)
    {
      g_object_set (stream, "combined-direction",
          MAKE_COMBINED_DIRECTION (TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL,
            0), NULL);
    }
  else
    {
      priv->awaiting_intersection = TRUE;
      g_object_set (stream, "combined-direction",
          MAKE_COMBINED_DIRECTION (TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL,
            TP_MEDIA_STREAM_PENDING_LOCAL_SEND), NULL);
    }

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
    case PROP_PEER:
      g_value_set_string (value, stream->peer);
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
    case PROP_CODECS_READY:
      g_value_set_boolean (value, priv->codecs != NULL);
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
    case PROP_PEER:
      g_free (stream->peer);
      stream->peer = g_value_dup_string (value);
      break;
    case PROP_ID:
      priv->id = g_value_get_uint (value);
      break;
    case PROP_MEDIA_TYPE:
      priv->media_type = g_value_get_uint (value);
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
      break;
    case PROP_NAT_TRAVERSAL:
      g_assert (priv->nat_traversal == NULL);
      priv->nat_traversal = g_value_dup_string (value);
      break;
    case PROP_CREATED_LOCALLY:
      priv->created_locally = g_value_get_boolean (value);
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
  static TpDBusPropertiesMixinPropImpl stream_handler_props[] = {
      { "RelayInfo", "relay-info", NULL },
      { "STUNServers", "stun-servers", NULL },
      { "NATTraversal", "nat-traversal", NULL },
      { "CreatedLocally", "created-locally", NULL },
      { NULL }
  };
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { TP_IFACE_MEDIA_STREAM_HANDLER,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        stream_handler_props,
      },
      { NULL }
  };
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
                                    G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_string ("name", "Stream name",
      "An opaque name for the stream used in the signalling.", NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_NAME, param_spec);

  param_spec = g_param_spec_string ("peer", "Peer name",
      "The name for the peer used in the signalling.", NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_NAME |
      G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_PEER, param_spec);

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
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
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
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CREATED_LOCALLY,
      param_spec);

  param_spec = g_param_spec_boolean ("codecs-ready", "Codecs ready",
      "True if the codecs for this stream are ready to be used", FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CODECS_READY,
      param_spec);

  haze_media_stream_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (HazeMediaStreamClass, dbus_props_class));
}

void
haze_media_stream_dispose (GObject *object)
{
  HazeMediaStream *self = HAZE_MEDIA_STREAM (object);
  HazeMediaStreamPrivate *priv = self->priv;

  DEBUG ("called");

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->local_candidates)
    {
      purple_media_candidate_list_free (priv->local_candidates);
      priv->local_candidates = NULL;
    }

  if (priv->remote_candidates)
    {
      purple_media_candidate_list_free (priv->remote_candidates);
      priv->remote_candidates = NULL;
    }

  if (priv->codecs)
    {
      purple_media_codec_list_free (priv->codecs);
      priv->codecs = NULL;
    }

  if (priv->remote_codecs)
    {
      purple_media_codec_list_free (priv->remote_codecs);
      priv->remote_codecs = NULL;
    }

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

  G_OBJECT_CLASS (haze_media_stream_parent_class)->finalize (object);
}


GList *
haze_media_stream_get_local_candidates (HazeMediaStream *self)
{
  HazeMediaStreamPrivate *priv = self->priv;
  return g_list_copy (priv->local_candidates);
}


GList *
haze_media_stream_get_codecs (HazeMediaStream *self)
{
  HazeMediaStreamPrivate *priv = self->priv;
  return purple_media_codec_list_copy (priv->codecs);
}


static void
pass_remote_candidates (HazeMediaStream *self)
{
  HazeMediaStreamPrivate *priv = self->priv;
  GType transport_struct_type = TP_STRUCT_TYPE_MEDIA_STREAM_HANDLER_TRANSPORT;
  GType candidate_struct_type = TP_STRUCT_TYPE_MEDIA_STREAM_HANDLER_CANDIDATE;
  GList *iter = priv->remote_candidates;

  for (; iter; iter = g_list_next (iter))
    {
      gchar *address, *username, *password, *candidate_id;
      GValue candidate = { 0, };
      GPtrArray *transports;
      GValue transport = { 0, };
      PurpleMediaCandidate *c = iter->data;
      PurpleMediaCandidateType candidate_type;
      guint type = TP_MEDIA_STREAM_TRANSPORT_TYPE_LOCAL;

      g_value_init (&transport, transport_struct_type);
      g_value_take_boxed (&transport,
          dbus_g_type_specialized_construct (transport_struct_type));

      address = purple_media_candidate_get_ip (c);
      username = purple_media_candidate_get_username (c);
      password = purple_media_candidate_get_password (c);
      candidate_type = purple_media_candidate_get_candidate_type (c);

      if (candidate_type == PURPLE_MEDIA_CANDIDATE_TYPE_HOST)
        type = TP_MEDIA_STREAM_TRANSPORT_TYPE_LOCAL;
      else if (candidate_type == PURPLE_MEDIA_CANDIDATE_TYPE_SRFLX)
        type = TP_MEDIA_STREAM_TRANSPORT_TYPE_DERIVED;
      else if (candidate_type == PURPLE_MEDIA_CANDIDATE_TYPE_RELAY)
        type = TP_MEDIA_STREAM_TRANSPORT_TYPE_RELAY;
      else
        DEBUG ("Unknown candidate type");

      dbus_g_type_struct_set (&transport,
          0, purple_media_candidate_get_component_id (c),
          1, address,
          2, purple_media_candidate_get_port (c),
          3, purple_media_candidate_get_protocol (c) ==
              PURPLE_MEDIA_NETWORK_PROTOCOL_UDP ?
              TP_MEDIA_STREAM_BASE_PROTO_UDP :
              TP_MEDIA_STREAM_BASE_PROTO_TCP,
          4, "RTP",
          5, "AVP",
          6, (double)purple_media_candidate_get_priority (c),
          7, type,
          8, username,
          9, password,
          G_MAXUINT);

      g_free (password);
      g_free (username);
      g_free (address);

      transports = g_ptr_array_sized_new (1);
      g_ptr_array_add (transports, g_value_get_boxed (&transport));

      g_value_init (&candidate, candidate_struct_type);
      g_value_take_boxed (&candidate,
          dbus_g_type_specialized_construct (candidate_struct_type));

      candidate_id = purple_media_candidate_get_foundation (c);

      dbus_g_type_struct_set (&candidate,
          0, candidate_id,
          1, transports,
          G_MAXUINT);

      DEBUG ("passing 1 remote candidate to stream engine: %s", candidate_id);

      tp_svc_media_stream_handler_emit_add_remote_candidate (
          self, candidate_id, transports);

      g_free (candidate_id);
    }
}


void
haze_media_stream_add_remote_candidates (HazeMediaStream *self,
                                         GList *remote_candidates)
{
  HazeMediaStreamPrivate *priv = self->priv;

  priv->remote_candidates = g_list_concat (
      priv->remote_candidates,
      purple_media_candidate_list_copy (remote_candidates));

  if (priv->ready == TRUE)
    pass_remote_candidates (self);
}


static void
pass_remote_codecs (HazeMediaStream *self)
{
  HazeMediaStreamPrivate *priv = self->priv;
  GType codec_struct_type = TP_STRUCT_TYPE_MEDIA_STREAM_HANDLER_CODEC;
  GPtrArray *codecs = g_ptr_array_new ();
  GList *iter = priv->remote_codecs;

  for (; iter; iter = g_list_next (iter))
    {
      GValue codec = { 0, };
      PurpleMediaCodec *c = iter->data;
      gchar *name;
      GList *codec_params;
      GHashTable *params;

      g_value_init (&codec, codec_struct_type);
      g_value_take_boxed (&codec,
          dbus_g_type_specialized_construct (codec_struct_type));

      name = purple_media_codec_get_encoding_name (c);
      codec_params = purple_media_codec_get_optional_parameters (c);
      params = g_hash_table_new_full (g_str_hash, g_str_equal,
          g_free, g_free);

      for (; codec_params; codec_params = g_list_next (codec_params))
        {
          PurpleKeyValuePair *pair = codec_params->data;
          g_hash_table_insert (params, pair->key, pair->value);
        }

      DEBUG ("new remote %s codec: %u '%s' %u %u %u",
          priv->media_type == TP_MEDIA_STREAM_TYPE_AUDIO ? "audio" : "video",
          purple_media_codec_get_id (c), name, priv->media_type,
          purple_media_codec_get_clock_rate (c),
          purple_media_codec_get_channels (c));

      dbus_g_type_struct_set (&codec,
          0, purple_media_codec_get_id (c),
          1, name,
          2, priv->media_type,
          3, purple_media_codec_get_clock_rate (c),
          4, purple_media_codec_get_channels (c),
          5, params,
          G_MAXUINT);

      g_free (name);
      g_hash_table_destroy (params);

      g_ptr_array_add (codecs, g_value_get_boxed (&codec));
   }

  DEBUG ("passing %d remote codecs to stream-engine", codecs->len);

  tp_svc_media_stream_handler_emit_set_remote_codecs (self, codecs);
}


void
haze_media_stream_set_remote_codecs (HazeMediaStream *self,
                                     GList *remote_codecs)
{
  HazeMediaStreamPrivate *priv = self->priv;
  GList *iter = priv->remote_codecs;

  for (; iter; iter = g_list_delete_link (iter, iter))
    g_object_unref (iter->data);

  priv->remote_codecs = purple_media_codec_list_copy (remote_codecs);

  if (priv->ready == TRUE)
    pass_remote_codecs (self);
}

void
haze_media_stream_add_stun_server (HazeMediaStream *self,
                                   const gchar *stun_ip,
                                   guint stun_port)
{
  HazeMediaStreamPrivate *priv = self->priv;
  GValueArray *va;
  GValue ip = {0}, port = {0};

  if (stun_ip == NULL || stun_ip[0] == 0)
    {
      DEBUG ("Invalid STUN address passed: %s", stun_ip);
      return;
    }
  else if (stun_port > 65535)
    {
      DEBUG ("Invalid STUN port passed: %d", stun_port);
      return;
    }

  g_value_init (&ip, G_TYPE_STRING);
  g_value_set_string (&ip, stun_ip);
  g_value_init (&port, G_TYPE_UINT);
  g_value_set_uint (&port, stun_port);

  va = g_value_array_new (2);
  g_value_array_append (va, &ip);
  g_value_array_append (va, &port);

  g_ptr_array_add (priv->stun_servers, va);
}


/**
 * haze_media_stream_codec_choice
 *
 * Implements D-Bus method CodecChoice
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
haze_media_stream_codec_choice (TpSvcMediaStreamHandler *iface,
                                guint codec_id,
                                DBusGMethodInvocation *context)
{
  HazeMediaStream *self = HAZE_MEDIA_STREAM (iface);
  HazeMediaStreamPrivate *priv;

  g_assert (HAZE_IS_MEDIA_STREAM (self));

  priv = self->priv;

  tp_svc_media_stream_handler_return_from_codec_choice (context);
}


gboolean
haze_media_stream_error (HazeMediaStream *self,
                         guint err_no,
                         const gchar *message,
                         GError **error)
{
  g_assert (HAZE_IS_MEDIA_STREAM (self));

  DEBUG ( "Media.StreamHandler::Error called, error %u (%s) -- emitting signal",
      err_no, message);

  purple_media_error (self->priv->media, message);

  return TRUE;
}


/**
 * haze_media_stream_error
 *
 * Implements D-Bus method Error
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
haze_media_stream_error_async (TpSvcMediaStreamHandler *iface,
                               guint errno,
                               const gchar *message,
                               DBusGMethodInvocation *context)
{
  HazeMediaStream *self = HAZE_MEDIA_STREAM (iface);
  GError *error = NULL;

  if (haze_media_stream_error (self, errno, message, &error))
    {
      tp_svc_media_stream_handler_return_from_error (context);
    }
  else
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}


/**
 * haze_media_stream_hold:
 *
 * Tell streaming clients that the stream is going on hold, so they should
 * stop streaming and free up any resources they are currently holding
 * (e.g. close hardware devices); or that the stream is coming off hold,
 * so they should reacquire those resources.
 */
void
haze_media_stream_hold (HazeMediaStream *self,
                        gboolean hold)
{
  tp_svc_media_stream_handler_emit_set_stream_held (self, hold);
}


/**
 * haze_media_stream_hold_state:
 *
 * Called by streaming clients when the stream's hold state has been changed
 * successfully in response to SetStreamHeld.
 */
static void
haze_media_stream_hold_state (TpSvcMediaStreamHandler *iface,
                              gboolean hold_state,
                              DBusGMethodInvocation *context)
{
  HazeMediaStream *self = HAZE_MEDIA_STREAM (iface);
  HazeMediaStreamPrivate *priv = self->priv;

  DEBUG ("%p: %s", self, hold_state ? "held" : "unheld");
  priv->local_hold = hold_state;

  g_object_notify ((GObject *) self, "local-hold");

  tp_svc_media_stream_handler_return_from_hold_state (context);
}


/**
 * haze_media_stream_unhold_failure:
 *
 * Called by streaming clients when an attempt to reacquire the necessary
 * hardware or software resources to unhold the stream, in response to
 * SetStreamHeld, has failed.
 */
static void
haze_media_stream_unhold_failure (TpSvcMediaStreamHandler *iface,
                                  DBusGMethodInvocation *context)
{
  HazeMediaStream *self = HAZE_MEDIA_STREAM (iface);
  HazeMediaStreamPrivate *priv = self->priv;

  DEBUG ("%p", self);

  priv->local_hold = TRUE;

//  maybe emit unhold failed here?
  g_object_notify ((GObject *) self, "local-hold");

  tp_svc_media_stream_handler_return_from_unhold_failure (context);
}


/**
 * haze_media_stream_native_candidates_prepared
 *
 * Implements D-Bus method NativeCandidatesPrepared
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
haze_media_stream_native_candidates_prepared (TpSvcMediaStreamHandler *iface,
                                              DBusGMethodInvocation *context)
{
  HazeMediaStream *self = HAZE_MEDIA_STREAM (iface);
  HazeMediaStreamPrivate *priv;
  PurpleMediaBackend *backend;

  g_assert (HAZE_IS_MEDIA_STREAM (self));

  priv = self->priv;

  g_object_get (G_OBJECT (priv->media), "backend", &backend, NULL);
  g_signal_emit_by_name (backend, "candidates-prepared",
      self->name, self->peer);
  g_object_unref (backend);

  tp_svc_media_stream_handler_return_from_native_candidates_prepared (context);
}


/**
 * haze_media_stream_new_active_candidate_pair
 *
 * Implements D-Bus method NewActiveCandidatePair
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
haze_media_stream_new_active_candidate_pair (TpSvcMediaStreamHandler *iface,
                                             const gchar *native_candidate_id,
                                             const gchar *remote_candidate_id,
                                             DBusGMethodInvocation *context)
{
  HazeMediaStream *self = HAZE_MEDIA_STREAM (iface);
  HazeMediaStreamPrivate *priv;
  PurpleMediaBackend *backend;
  GList *l_iter;

  /*
   * This appears to be called for each pair of components.
   * I'm not sure how to go about differentiating between the two
   * components as the ids are the same.
   */

  DEBUG ("called (%s, %s)", native_candidate_id, remote_candidate_id);

  g_assert (HAZE_IS_MEDIA_STREAM (self));

  priv = self->priv;
  l_iter = priv->local_candidates;
  g_object_get (priv->media, "backend", &backend, NULL);

  for (; l_iter; l_iter = g_list_next (l_iter))
    {
      PurpleMediaCandidate *lc = l_iter->data;
      GList *r_iter = priv->remote_candidates;

      for (; r_iter; r_iter = g_list_next (r_iter))
        {
          PurpleMediaCandidate *rc = r_iter->data;

          if (purple_media_candidate_get_component_id (lc) ==
              purple_media_candidate_get_component_id (rc))
            {
              gchar *l_name = purple_media_candidate_get_foundation (lc);
              gchar *r_name = purple_media_candidate_get_foundation (rc);

              if (!strcmp (l_name, native_candidate_id) &&
                  !strcmp (r_name, remote_candidate_id))
                {
                  DEBUG ("Emitting new active candidate pair %d: %s - %s",
                      purple_media_candidate_get_component_id (lc),
                      l_name, r_name);

                  g_signal_emit_by_name (backend, "active-candidate-pair",
                      self->name, self->peer, lc, rc);
                }

              g_free (l_name);
              g_free (r_name);
            }
        }
    }

  g_object_unref (backend);
  tp_svc_media_stream_handler_return_from_new_active_candidate_pair (context);
}


/**
 * haze_media_stream_new_native_candidate
 *
 * Implements D-Bus method NewNativeCandidate
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
haze_media_stream_new_native_candidate (TpSvcMediaStreamHandler *iface,
                                        const gchar *candidate_id,
                                        const GPtrArray *transports,
                                        DBusGMethodInvocation *context)
{
  HazeMediaStream *self = HAZE_MEDIA_STREAM (iface);
  HazeMediaStreamPrivate *priv;
  PurpleMediaBackend *backend;
  guint i;

  g_assert (HAZE_IS_MEDIA_STREAM (self));

  priv = self->priv;

  g_object_get (G_OBJECT (priv->media), "backend", &backend, NULL);

  for (i = 0; i < transports->len; i++)
    {
      GValueArray *transport;
      guint component, type, proto;
      PurpleMediaCandidate *c;
      PurpleMediaCandidateType candidate_type =
          PURPLE_MEDIA_CANDIDATE_TYPE_HOST;
      PurpleMediaNetworkProtocol protocol = PURPLE_MEDIA_NETWORK_PROTOCOL_UDP;

      transport = g_ptr_array_index (transports, i);
      component = g_value_get_uint (g_value_array_get_nth (transport, 0));
      type = g_value_get_uint (g_value_array_get_nth (transport, 7));
      proto = g_value_get_uint (g_value_array_get_nth (transport, 3));

      if (type == TP_MEDIA_STREAM_TRANSPORT_TYPE_LOCAL)
        candidate_type = PURPLE_MEDIA_CANDIDATE_TYPE_HOST;
      else if (type == TP_MEDIA_STREAM_TRANSPORT_TYPE_DERIVED)
        candidate_type = PURPLE_MEDIA_CANDIDATE_TYPE_SRFLX;
      else if (type == TP_MEDIA_STREAM_TRANSPORT_TYPE_RELAY)
        candidate_type = PURPLE_MEDIA_CANDIDATE_TYPE_RELAY;
      else
        DEBUG ("Unknown candidate type");

      if (proto == TP_MEDIA_STREAM_BASE_PROTO_UDP)
        protocol = PURPLE_MEDIA_NETWORK_PROTOCOL_UDP;
      else if (proto == TP_MEDIA_STREAM_BASE_PROTO_TCP)
        protocol = PURPLE_MEDIA_NETWORK_PROTOCOL_TCP;
      else
        DEBUG ("Unknown network protocol");

      c = purple_media_candidate_new (candidate_id, component, candidate_type,
          protocol,
          /* address */
          g_value_get_string (g_value_array_get_nth (transport, 1)),
          /* port */
          g_value_get_uint (g_value_array_get_nth (transport, 2)));

      g_object_set (c, "username",
          g_value_get_string (g_value_array_get_nth (transport, 8)), NULL);
      g_object_set (c, "password",
          g_value_get_string (g_value_array_get_nth (transport, 9)), NULL);
      g_object_set (c, "priority",
          (guint)g_value_get_double (
          g_value_array_get_nth (transport, 6)), NULL);

      DEBUG ("new-candidate: %s %s %p", self->name, self->peer, c);

      priv->local_candidates = g_list_append (priv->local_candidates, c);

      g_signal_emit_by_name (backend, "new-candidate", self->name, self->peer, c);
    }

  g_object_unref (backend);

  tp_svc_media_stream_handler_return_from_new_native_candidate (context);
}

static void haze_media_stream_set_local_codecs (TpSvcMediaStreamHandler *,
    const GPtrArray *codecs, DBusGMethodInvocation *);

/**
 * haze_media_stream_ready
 *
 * Implements D-Bus method Ready
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
haze_media_stream_ready (TpSvcMediaStreamHandler *iface,
                         const GPtrArray *codecs,
                         DBusGMethodInvocation *context)
{
  HazeMediaStream *self = HAZE_MEDIA_STREAM (iface);
  HazeMediaStreamPrivate *priv;

  g_assert (HAZE_IS_MEDIA_STREAM (self));

  priv = self->priv;

  DEBUG ("ready called");

  if (priv->ready == FALSE)
    {
      g_object_set (self, "ready", TRUE, NULL);

      tp_svc_media_stream_handler_emit_set_stream_playing (self, TRUE);

      if (purple_media_get_session_type (priv->media, self->name) &
          (PURPLE_MEDIA_SEND_AUDIO | PURPLE_MEDIA_SEND_VIDEO))
        {
          g_object_set (self, "combined-direction",
              MAKE_COMBINED_DIRECTION (TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL,
                0), NULL);
          tp_svc_media_stream_handler_emit_set_stream_sending (self, TRUE);
        }

      /* If a new stream is added while the call's on hold, it will have
       * local_hold set at construct time. So once tp-fs has called Ready(), we
       * should let it know this stream's on hold.
       */
      if (priv->local_hold)
        haze_media_stream_hold (self, priv->local_hold);
    }
  else
    {
      DEBUG ("Ready called twice, running plain SetLocalCodecs instead");
    }

  /* set_local_codecs and ready return the same thing, so we can do... */
  haze_media_stream_set_local_codecs (iface, codecs, context);
  pass_remote_codecs (self);
  pass_remote_candidates (self);
}

static void
convert_param (gchar *key, gchar *value, PurpleMediaCodec *codec)
{
  purple_media_codec_add_optional_parameter (codec, key, value);
}

static gboolean
pass_local_codecs (HazeMediaStream *stream,
                   const GPtrArray *codecs,
                   gboolean ready,
                   GError **error)
{
  HazeMediaStreamPrivate *priv = stream->priv;
  PurpleMediaSessionType type = PURPLE_MEDIA_AUDIO;
  PurpleMediaCodec *c;
  guint i;

  DEBUG ("putting list of %d supported codecs from stream-engine into cache",
      codecs->len);

  if (priv->media_type == TP_MEDIA_STREAM_TYPE_AUDIO)
    type = PURPLE_MEDIA_AUDIO;
  else if (priv->media_type == TP_MEDIA_STREAM_TYPE_VIDEO)
    type = PURPLE_MEDIA_VIDEO;
  else
    g_assert_not_reached ();

  for (i = 0; i < codecs->len; i++)
    {
      GType codec_struct_type = TP_STRUCT_TYPE_MEDIA_STREAM_HANDLER_CODEC;

      GValue codec = { 0, };
      guint id, clock_rate, channels;
      gchar *name;
      GHashTable *params;

      g_value_init (&codec, codec_struct_type);
      g_value_set_static_boxed (&codec, g_ptr_array_index (codecs, i));

      dbus_g_type_struct_get (&codec,
          0, &id,
          1, &name,
          3, &clock_rate,
          4, &channels,
          5, &params,
          G_MAXUINT);

      c = purple_media_codec_new (id, name, type, clock_rate);
      g_object_set (c, "channels", channels, NULL);

      g_hash_table_foreach (params, (GHFunc)convert_param, c);

      DEBUG ("adding codec: %s", purple_media_codec_to_string (c));

      priv->codecs = g_list_append (priv->codecs, c);

      g_signal_emit_by_name (priv->media, "codecs-changed", stream->name);
    }

  return TRUE;
}

/**
 * haze_media_stream_set_local_codecs
 *
 * Implements D-Bus method SetLocalCodecs
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
haze_media_stream_set_local_codecs (TpSvcMediaStreamHandler *iface,
                                    const GPtrArray *codecs,
                                    DBusGMethodInvocation *context)
{
  HazeMediaStream *self = HAZE_MEDIA_STREAM (iface);
  HazeMediaStreamPrivate *priv = self->priv;
  GError *error = NULL;

  DEBUG ("called");

  if (PURPLE_IS_MEDIA (priv->media) &&
      purple_media_is_initiator (priv->media, self->name, self->peer))
    {
      if (!pass_local_codecs (self, codecs, self->priv->created_locally,
          &error))
        {
          DEBUG ("failed: %s", error->message);

          dbus_g_method_return_error (context, error);
          g_error_free (error);
          return;
        }
    }
  else
    {
      DEBUG ("ignoring local codecs, waiting for codec intersection");
    }

  tp_svc_media_stream_handler_return_from_set_local_codecs (context);
}

/**
 * haze_media_stream_stream_state
 *
 * Implements D-Bus method StreamState
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
haze_media_stream_stream_state (TpSvcMediaStreamHandler *iface,
                                guint connection_state,
                                DBusGMethodInvocation *context)
{
  HazeMediaStream *self = HAZE_MEDIA_STREAM (iface);
  PurpleMediaState media_state = PURPLE_MEDIA_STATE_END;

  switch (connection_state) {
    case TP_MEDIA_STREAM_STATE_DISCONNECTED:
      media_state = PURPLE_MEDIA_STATE_END;
      break;
    case TP_MEDIA_STREAM_STATE_CONNECTING:
      media_state = PURPLE_MEDIA_STATE_NEW;
      break;
    case TP_MEDIA_STREAM_STATE_CONNECTED:
      media_state = PURPLE_MEDIA_STATE_CONNECTED;
      break;
    default:
      DEBUG ("ignoring unknown connection state %u", connection_state);
      goto OUT;
  }

  g_object_set (self, "connection-state", connection_state, NULL);

  // emit connection state here

OUT:
  tp_svc_media_stream_handler_return_from_stream_state (context);
}


/**
 * haze_media_stream_supported_codecs
 *
 * Implements D-Bus method SupportedCodecs
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
haze_media_stream_supported_codecs (TpSvcMediaStreamHandler *iface,
                                    const GPtrArray *codecs,
                                    DBusGMethodInvocation *context)
{
  HazeMediaStream *self = HAZE_MEDIA_STREAM (iface);
  HazeMediaStreamPrivate *priv = self->priv;
  GError *error = NULL;

  DEBUG ("called");

  if (priv->awaiting_intersection)
    {
      if (!pass_local_codecs (self, codecs, TRUE, &error))
        {
          DEBUG ("failed: %s", error->message);

          dbus_g_method_return_error (context, error);
          g_error_free (error);
          return;
        }

      priv->awaiting_intersection = FALSE;
    }
  else
    {
      /* If we created the stream, we don't need to send the intersection. If
       * we didn't create it, but have already sent the intersection once, we
       * don't need to send it again. In either case, extra calls to
       * SupportedCodecs are in response to an incoming description-info, which
       * can only change parameters and which XEP-0167 §10 says is purely
       * advisory.
       */
      DEBUG ("we already sent, or don't need to send, our codecs");
    }

  tp_svc_media_stream_handler_return_from_supported_codecs (context);
}

/**
 * haze_media_stream_codecs_updated
 *
 * Implements D-Bus method CodecsUpdated
 * on interface org.freedesktop.Telepathy.Media.StreamHandler
 */
static void
haze_media_stream_codecs_updated (TpSvcMediaStreamHandler *iface,
                                  const GPtrArray *codecs,
                                  DBusGMethodInvocation *context)
{
  HazeMediaStream *self = HAZE_MEDIA_STREAM (iface);
  GError *error = NULL;

  if (self->priv->codecs == NULL)
    {
      GError e = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
          "CodecsUpdated may only be called once an initial set of codecs "
          "has been set" };

      dbus_g_method_return_error (context, &e);
      return;
    }

  if (self->priv->awaiting_intersection)
    {
      /* When awaiting an intersection the initial set of codecs should be set
       * by calling SupportedCodecs as that is the canonical set of codecs,
       * updates are only meaningful afterwards */
      tp_svc_media_stream_handler_return_from_codecs_updated (context);
      return;
    }

  if (pass_local_codecs (self, codecs, self->priv->created_locally, &error))
    {
      tp_svc_media_stream_handler_return_from_codecs_updated (context);
    }
  else
    {
      DEBUG ("failed: %s", error->message);

      dbus_g_method_return_error (context, error);
      g_error_free (error);
    }
}

static void
stream_handler_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcMediaStreamHandlerClass *klass =
    (TpSvcMediaStreamHandlerClass *) g_iface;

#define IMPLEMENT(x,suffix) tp_svc_media_stream_handler_implement_##x (\
    klass, haze_media_stream_##x##suffix)
  IMPLEMENT(codec_choice,);
  IMPLEMENT(error,_async);
  IMPLEMENT(hold_state,);
  IMPLEMENT(native_candidates_prepared,);
  IMPLEMENT(new_active_candidate_pair,);
  IMPLEMENT(new_native_candidate,);
  IMPLEMENT(ready,);
  IMPLEMENT(set_local_codecs,);
  IMPLEMENT(stream_state,);
  IMPLEMENT(supported_codecs,);
  IMPLEMENT(unhold_failure,);
  IMPLEMENT(codecs_updated,);
#undef IMPLEMENT
}
