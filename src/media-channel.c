/*
 * media-channel.c - Source for HazeMediaChannel
 * Copyright (C) 2006, 2009 Collabora Ltd.
 * Copyright (C) 2006 Nokia Corporation
 *
 * Copied heavily from telepathy-gabble
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
#include "media-channel.h"

#include <libpurple/media/backend-iface.h>
#include <libpurple/mediamanager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/svc-channel.h>
#include <telepathy-glib/svc-properties-interface.h>
#include <telepathy-glib/svc-media-interfaces.h>

#include "connection.h"
#include "debug.h"
#include "media-backend.h"
#include "media-stream.h"

static void channel_iface_init (gpointer, gpointer);
static void media_signalling_iface_init (gpointer, gpointer);
static void streamed_media_iface_init (gpointer, gpointer);
static gboolean haze_media_channel_add_member (GObject *obj,
    TpHandle handle,
    const gchar *message,
    GError **error);
static gboolean haze_media_channel_remove_member (GObject *obj,
    TpHandle handle, const gchar *message, guint reason, GError **error);

G_DEFINE_TYPE_WITH_CODE (HazeMediaChannel, haze_media_channel,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
      tp_group_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_STREAMED_MEDIA,
      streamed_media_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_MEDIA_SIGNALLING,
      media_signalling_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
      tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL));

static const gchar *haze_media_channel_interfaces[] = {
    TP_IFACE_CHANNEL_INTERFACE_GROUP,
    TP_IFACE_CHANNEL_INTERFACE_MEDIA_SIGNALLING,
    NULL
};

/* properties */
enum
{
  PROP_OBJECT_PATH = 1,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_TARGET_ID,
  PROP_INITIAL_PEER,
  PROP_PEER,
  PROP_REQUESTED,
  PROP_CONNECTION,
  PROP_CREATOR,
  PROP_CREATOR_ID,
  PROP_INTERFACES,
  PROP_CHANNEL_DESTROYED,
  PROP_CHANNEL_PROPERTIES,
  PROP_INITIAL_AUDIO,
  PROP_INITIAL_VIDEO,
  PROP_MEDIA,
  LAST_PROPERTY
};

struct _HazeMediaChannelPrivate
{
  HazeConnection *conn;
  gchar *object_path;
  TpHandle creator;
  TpHandle initial_peer;

  PurpleMedia *media;

  guint next_stream_id;

  /* list of PendingStreamRequest* in no particular order */
  GList *pending_stream_requests;

  TpLocalHoldState hold_state;
  TpLocalHoldStateReason hold_state_reason;

  TpChannelCallStateFlags call_state;

  gboolean initial_audio;
  gboolean initial_video;

  gboolean ready;
  gboolean media_ended;
  gboolean closed;
  gboolean dispose_has_run;
};

static void
haze_media_channel_init (HazeMediaChannel *self)
{
  HazeMediaChannelPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      HAZE_TYPE_MEDIA_CHANNEL, HazeMediaChannelPrivate);

  self->priv = priv;

  priv->next_stream_id = 1;
}

/**
 * make_stream_list:
 *
 * Creates an array of MediaStreamInfo structs.
 */
static GPtrArray *
make_stream_list (HazeMediaChannel *self,
                  guint len,
                  HazeMediaStream **streams)
{
  HazeMediaChannelPrivate *priv = self->priv;
  GPtrArray *ret;
  guint i;
  GType info_type = TP_STRUCT_TYPE_MEDIA_STREAM_INFO;

  ret = g_ptr_array_sized_new (len);

  for (i = 0; i < len; i++)
    {
      GValue entry = { 0, };
      guint id;
      TpHandle peer;
      TpMediaStreamType type;
      TpMediaStreamState connection_state;
      CombinedStreamDirection combined_direction;

      g_object_get (streams[i],
          "id", &id,
          "media-type", &type,
          "connection-state", &connection_state,
          "combined-direction", &combined_direction,
          NULL);

      peer = priv->initial_peer;

      g_value_init (&entry, info_type);
      g_value_take_boxed (&entry,
          dbus_g_type_specialized_construct (info_type));

      dbus_g_type_struct_set (&entry,
          0, id,
          1, peer,
          2, type,
          3, connection_state,
          4, COMBINED_DIRECTION_GET_DIRECTION (combined_direction),
          5, COMBINED_DIRECTION_GET_PENDING_SEND (combined_direction),
          G_MAXUINT);

      g_ptr_array_add (ret, g_value_get_boxed (&entry));
    }

  return ret;
}

typedef struct {
    /* number of streams requested == number of content objects */
    guint len;
    /* array of @len borrowed pointers */
    guint *types;
    /* accumulates borrowed pointers to streams. Initially @len NULL pointers;
     * when the stream for contents[i] is created, it is stored at streams[i].
     */
    HazeMediaStream **streams;
    /* number of non-NULL elements in streams (0 <= satisfied <= contents) */
    guint satisfied;
    /* succeeded_cb(context, GPtrArray<TP_STRUCT_TYPE_MEDIA_STREAM_INFO>)
     * will be called if the stream request succeeds.
     */
    GFunc succeeded_cb;
    /* failed_cb(context, GError *) will be called if the stream request fails.
     */
    GFunc failed_cb;
    gpointer context;
} PendingStreamRequest;

static PendingStreamRequest *
pending_stream_request_new (const GArray *types,
    GFunc succeeded_cb,
    GFunc failed_cb,
    gpointer context)
{
  PendingStreamRequest *p = g_slice_new0 (PendingStreamRequest);

  g_assert (succeeded_cb);
  g_assert (failed_cb);

  p->len = types->len;
  p->types = g_memdup (types->data, types->len * sizeof (gpointer));
  p->streams = g_new0 (HazeMediaStream *, types->len);
  p->satisfied = 0;
  p->succeeded_cb = succeeded_cb;
  p->failed_cb = failed_cb;
  p->context = context;

  return p;
}

static gboolean
pending_stream_request_maybe_satisfy (PendingStreamRequest *p,
                                      HazeMediaChannel *channel,
                                      guint type,
                                      HazeMediaStream *stream)
{
  guint i;

  for (i = 0; i < p->len; i++)
    {
      if (p->types[i] == type)
        {
          g_assert (p->streams[i] == NULL);
          p->streams[i] = stream;

          if (++p->satisfied == p->len && p->context != NULL)
            {
              GPtrArray *ret = make_stream_list (channel, p->len, p->streams);

              p->succeeded_cb (p->context, ret);
              g_ptr_array_foreach (ret, (GFunc) g_value_array_free, NULL);
              g_ptr_array_free (ret, TRUE);
              p->context = NULL;
              return TRUE;
            }
        }
    }

  return FALSE;
}

static void
pending_stream_request_free (gpointer data)
{
  PendingStreamRequest *p = data;

  if (p->context != NULL)
    {
      GError e = { TP_ERRORS, TP_ERROR_CANCELLED,
          "The session terminated before the requested streams could be added"
      };

      p->failed_cb (p->context, &e);
    }

  g_free (p->types);
  g_free (p->streams);

  g_slice_free (PendingStreamRequest, p);
}

static void
stream_direction_changed_cb (HazeMediaStream *stream,
                             GParamSpec *pspec,
                             HazeMediaChannel *chan)
{
  guint id;
  CombinedStreamDirection combined;
  TpMediaStreamDirection direction;
  TpMediaStreamPendingSend pending_send;

  g_object_get (stream,
      "id", &id,
      "combined-direction", &combined,
      NULL);

  direction = COMBINED_DIRECTION_GET_DIRECTION (combined);
  pending_send = COMBINED_DIRECTION_GET_PENDING_SEND (combined);

  DEBUG ("direction: %u, pending_send: %u", direction, pending_send);

  tp_svc_channel_type_streamed_media_emit_stream_direction_changed (
      chan, id, direction, pending_send);
}

static void
media_error_cb (PurpleMedia *media,
                const gchar *error,
                HazeMediaChannel *chan)
{
  g_assert (HAZE_MEDIA_CHANNEL(chan)->priv != NULL);
  DEBUG ("Media error on %s: %s", chan->priv->object_path, error);
}

static void
media_state_changed_cb (PurpleMedia *media,
                        PurpleMediaState state,
                        gchar *sid, gchar *name,
                        HazeMediaChannel *chan)
{
  HazeMediaChannelPrivate *priv = chan->priv;

  DEBUG ("%s %s %s",
      state == PURPLE_MEDIA_STATE_NEW ? "NEW" :
      state == PURPLE_MEDIA_STATE_CONNECTED ? "CONNECTED" :
      state == PURPLE_MEDIA_STATE_END ? "END" :
      "UNKNOWN", sid, name);

  if (state == PURPLE_MEDIA_STATE_NEW)
    {
      if (sid != NULL && name != NULL)
        {
          HazeMediaBackend *backend;
          HazeMediaStream *stream;
          TpMediaStreamType type;
          guint id;

          g_object_get (priv->media, "backend", &backend, NULL);
          stream = haze_media_backend_get_stream_by_name (backend, sid);
          g_object_unref (backend);

          g_object_get (G_OBJECT (stream), "id", &id, NULL);
          type = haze_media_stream_get_media_type (stream);

          /* if any RequestStreams call was waiting for a stream to be created for
           * that content, return from it successfully */
            {
              GList *iter = priv->pending_stream_requests;

              while (iter != NULL)
               {
                  if (pending_stream_request_maybe_satisfy (iter->data,
                        chan, type, stream))
                    {
                      GList *dead = iter;

                      pending_stream_request_free (dead->data);

                      iter = dead->next;
                      priv->pending_stream_requests = g_list_delete_link (
                          priv->pending_stream_requests, dead);
                    }
                  else
                    {
                      iter = iter->next;
                    }
                }
            }

          g_signal_connect (stream, "notify::combined-direction",
              (GCallback) stream_direction_changed_cb, chan);

          tp_svc_channel_type_streamed_media_emit_stream_added (
              chan, id, priv->initial_peer, type);

          stream_direction_changed_cb (stream, NULL, chan);
        }
    }

  if (sid != NULL && name == NULL)
    {
      TpMediaStreamState tp_state;
      HazeMediaBackend *backend;
      HazeMediaStream *stream;

      if (state == PURPLE_MEDIA_STATE_NEW)
        tp_state = TP_MEDIA_STREAM_STATE_CONNECTING;
      else if (state == PURPLE_MEDIA_STATE_CONNECTED)
        tp_state = TP_MEDIA_STREAM_STATE_CONNECTED;
      else if (state == PURPLE_MEDIA_STATE_END)
        tp_state = TP_MEDIA_STREAM_STATE_DISCONNECTED;
      else
        {
          DEBUG ("Invalid state %d", state);
          return;
        }

      g_object_get (G_OBJECT (priv->media), "backend", &backend, NULL);
      stream = haze_media_backend_get_stream_by_name (backend, sid);
      g_object_unref (backend);

      if (stream != NULL)
        {
          guint id;
          g_object_get (stream, "id", &id, NULL);
          tp_svc_channel_type_streamed_media_emit_stream_state_changed (chan,
              id, tp_state);
        }
    }

  if (state == PURPLE_MEDIA_STATE_END)
    {
      if (sid != NULL && name == NULL)
        {
          HazeMediaBackend *backend;
          HazeMediaStream *stream;

          g_object_get (G_OBJECT (priv->media), "backend", &backend, NULL);
          stream = haze_media_backend_get_stream_by_name (backend, sid);
          g_object_unref (backend);

          if (stream != NULL)
            {
              guint id;
              g_object_get (stream, "id", &id, NULL);
              tp_svc_channel_type_streamed_media_emit_stream_removed (
                  chan, id);
            }
        }
      else if (sid == NULL && name == NULL)
        {
          TpGroupMixin *mixin = TP_GROUP_MIXIN (chan);
          guint terminator;
          TpHandle peer;
          TpIntSet *set;

          priv->media_ended = TRUE;

          peer = priv->initial_peer;

          /*
           * Primarily, sessions will be ended with hangup or reject. Any that
           * aren't are because of local errors so set the terminator to self.
           */
          terminator = mixin->self_handle;

          set = tp_intset_new ();

          /* remove us and the peer from the member list */
          tp_intset_add (set, mixin->self_handle);
          tp_intset_add (set, peer);

          tp_group_mixin_change_members ((GObject *) chan,
              "Media session ended", NULL, set, NULL, NULL,
              terminator, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

          tp_intset_destroy (set);

          /* any contents that we were waiting for have now lost */
          g_list_foreach (priv->pending_stream_requests,
              (GFunc) pending_stream_request_free, NULL);
          g_list_free (priv->pending_stream_requests);
          priv->pending_stream_requests = NULL;

          if (!priv->closed)
            {
               DEBUG ("calling media channel close from state changed cb");
               haze_media_channel_close (chan);
            }
        }
    }
}

static void
media_stream_info_cb(PurpleMedia *media,
                     PurpleMediaInfoType type,
                     gchar *sid,
                     gchar *name,
                     gboolean local,
                     HazeMediaChannel *chan)
{
  HazeMediaChannelPrivate *priv = chan->priv;
  TpBaseConnection *conn = (TpBaseConnection *)priv->conn;

  if (type == PURPLE_MEDIA_INFO_ACCEPT)
    {
      TpIntSet *set;
      TpHandle actor;

      if (local == FALSE)
          actor = priv->initial_peer;
      else
          actor = conn->self_handle;

      set = tp_intset_new_containing (actor);

      /* add the peer to the member list */
      tp_group_mixin_change_members (G_OBJECT (chan), "", set, NULL, NULL,
          NULL, actor, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

      if (sid != NULL && name == NULL && purple_media_is_initiator (
          media, sid, name) == FALSE)
        {
          HazeMediaBackend *backend;
          HazeMediaStream *stream;

          g_object_get (G_OBJECT (priv->media), "backend", &backend, NULL);
          stream = haze_media_backend_get_stream_by_name (backend, sid);
          g_object_unref (backend);

          g_object_set (stream, "combined-direction",
              MAKE_COMBINED_DIRECTION (
              TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL, 0), NULL);
        }
    }
  else if (type == PURPLE_MEDIA_INFO_REJECT ||
      type == PURPLE_MEDIA_INFO_HANGUP)
    {
      TpGroupMixin *mixin = TP_GROUP_MIXIN (chan);
      guint terminator;
      TpIntSet *set;

      if (sid != NULL)
        return;

      if (local == TRUE)
        terminator = conn->self_handle;
      else
        /* This will need to get the handle from name for multi-user calls */
        terminator = priv->initial_peer;

      set = tp_intset_new ();

      if (name != NULL)
          /* Remove participant */
          tp_intset_add (set, priv->initial_peer);
      else
          /* Remove us */
          tp_intset_add (set, mixin->self_handle);

      tp_group_mixin_change_members ((GObject *) chan,
          NULL, NULL, set, NULL, NULL, terminator,
          TP_CHANNEL_GROUP_CHANGE_REASON_NONE);

      tp_intset_destroy (set);
    }
}

static void
_latch_to_session (HazeMediaChannel *chan)
{
  HazeMediaChannelPrivate *priv = chan->priv;
  HazeMediaBackend *backend;
  gchar *object_path;

  g_assert (priv->media != NULL);

  DEBUG ("%p: Latching onto session %p", chan, priv->media);

  g_signal_connect(G_OBJECT(priv->media), "error",
       G_CALLBACK(media_error_cb), chan);
  g_signal_connect(G_OBJECT(priv->media), "state-changed",
       G_CALLBACK(media_state_changed_cb), chan);
  g_signal_connect(G_OBJECT(priv->media), "stream-info",
       G_CALLBACK(media_stream_info_cb), chan);

  object_path = g_strdup_printf ("%s/MediaSession0", priv->object_path);

  g_object_get (G_OBJECT (priv->media), "backend", &backend, NULL);
  g_object_set (G_OBJECT (backend), "object-path", object_path, NULL);
  g_object_unref (backend);

  tp_svc_channel_interface_media_signalling_emit_new_session_handler (
      G_OBJECT (chan), object_path, "rtp");

  g_free (object_path);
}

static GObject *
haze_media_channel_constructor (GType type, guint n_props,
                                GObjectConstructParam *props)
{
  GObject *obj;
  HazeMediaChannelPrivate *priv;
  TpBaseConnection *conn;
  DBusGConnection *bus;
  TpIntSet *set;
  TpHandleRepoIface *contact_handles;

  obj = G_OBJECT_CLASS (haze_media_channel_parent_class)->
      constructor (type, n_props, props);

  priv = HAZE_MEDIA_CHANNEL (obj)->priv;
  conn = (TpBaseConnection *) priv->conn;
  contact_handles = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);

  /* register object on the bus */
  bus = tp_get_bus ();
  dbus_g_connection_register_g_object (bus, priv->object_path, obj);

  tp_group_mixin_init (obj, G_STRUCT_OFFSET (HazeMediaChannel, group),
      contact_handles, conn->self_handle);

  if (priv->media != NULL)
      priv->creator = priv->initial_peer;
  else
      priv->creator = conn->self_handle;

  /* automatically add creator to channel, but also ref them again (because
   * priv->creator is the InitiatorHandle) */
  g_assert (priv->creator != 0);
  tp_handle_ref (contact_handles, priv->creator);

  set = tp_intset_new_containing (priv->creator);
  tp_group_mixin_change_members (obj, "", set, NULL, NULL, NULL, 0,
      TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
  tp_intset_destroy (set);

  /* We implement the 0.17.6 properties correctly, and can include a message
   * when ending a call.
   */
  tp_group_mixin_change_flags (obj,
      TP_CHANNEL_GROUP_FLAG_PROPERTIES |
      TP_CHANNEL_GROUP_FLAG_MESSAGE_REMOVE |
      TP_CHANNEL_GROUP_FLAG_MESSAGE_REJECT |
      TP_CHANNEL_GROUP_FLAG_MESSAGE_RESCIND,
      0);


  if (priv->media != NULL)
    {
      /* This is an incoming call; make us local pending and don't set any
       * group flags (all we can do is add or remove ourselves, which is always
       * valid per the spec)
       */
      set = tp_intset_new_containing (conn->self_handle);
      tp_group_mixin_change_members (obj, "", NULL, NULL, set, NULL,
          priv->initial_peer, TP_CHANNEL_GROUP_CHANGE_REASON_INVITED);
      tp_intset_destroy (set);

      /* Set up signal callbacks */
      _latch_to_session (HAZE_MEDIA_CHANNEL (obj));
    }

  return obj;
}

static void
haze_media_channel_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  HazeMediaChannel *chan = HAZE_MEDIA_CHANNEL (object);
  HazeMediaChannelPrivate *priv = chan->priv;
  TpBaseConnection *base_conn = (TpBaseConnection *) priv->conn;

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_CHANNEL_TYPE:
      g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA);
      break;
    case PROP_HANDLE_TYPE:
      /* This is used to implement TargetHandleType, which is immutable.  If
       * the peer was known at channel-creation time, this will be Contact;
       * otherwise, it must be None even if we subsequently learn who the peer
       * is.
       */
      if (priv->initial_peer != 0)
        g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
      else
        g_value_set_uint (value, TP_HANDLE_TYPE_NONE);
      break;
    case PROP_INITIAL_PEER:
    case PROP_HANDLE:
      /* As above: TargetHandle is immutable, so non-0 only if the peer handle
       * was known at creation time.
       */
      g_value_set_uint (value, priv->initial_peer);
      break;
    case PROP_TARGET_ID:
      /* As above. */
      if (priv->initial_peer != 0)
        {
          TpHandleRepoIface *repo = tp_base_connection_get_handles (
              base_conn, TP_HANDLE_TYPE_CONTACT);
          const gchar *target_id = tp_handle_inspect (repo, priv->initial_peer);

          g_value_set_string (value, target_id);
        }
      else
        {
          g_value_set_static_string (value, "");
        }

      break;
    case PROP_PEER:
      {
        TpHandle peer = 0;

        if (priv->initial_peer != 0)
          peer = priv->initial_peer;

        g_value_set_uint (value, peer);
        break;
      }
    case PROP_CONNECTION:
      g_value_set_object (value, priv->conn);
      break;
    case PROP_CREATOR:
      g_value_set_uint (value, priv->creator);
      break;
    case PROP_CREATOR_ID:
        {
          TpHandleRepoIface *repo = tp_base_connection_get_handles (
              base_conn, TP_HANDLE_TYPE_CONTACT);

          g_value_set_string (value, tp_handle_inspect (repo, priv->creator));
        }
      break;
    case PROP_REQUESTED:
      g_value_set_boolean (value, (priv->creator == base_conn->self_handle));
      break;
    case PROP_INTERFACES:
      g_value_set_boxed (value, haze_media_channel_interfaces);
      break;
    case PROP_CHANNEL_DESTROYED:
      g_value_set_boolean (value, priv->closed);
      break;
    case PROP_CHANNEL_PROPERTIES:
      g_value_take_boxed (value,
          tp_dbus_properties_mixin_make_properties_hash (object,
              TP_IFACE_CHANNEL, "TargetHandle",
              TP_IFACE_CHANNEL, "TargetHandleType",
              TP_IFACE_CHANNEL, "ChannelType",
              TP_IFACE_CHANNEL, "TargetID",
              TP_IFACE_CHANNEL, "InitiatorHandle",
              TP_IFACE_CHANNEL, "InitiatorID",
              TP_IFACE_CHANNEL, "Requested",
              TP_IFACE_CHANNEL, "Interfaces",
              TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA, "InitialAudio",
              TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA, "InitialVideo",
              NULL));
      break;
    case PROP_MEDIA:
      g_value_set_object (value, priv->media);
      break;
    case PROP_INITIAL_AUDIO:
      g_value_set_boolean (value, priv->initial_audio);
      break;
    case PROP_INITIAL_VIDEO:
      g_value_set_boolean (value, priv->initial_video);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void
haze_media_channel_set_property (GObject     *object,
                                 guint        property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  HazeMediaChannel *chan = HAZE_MEDIA_CHANNEL (object);
  HazeMediaChannelPrivate *priv = chan->priv;

  switch (property_id) {
    case PROP_OBJECT_PATH:
      g_free (priv->object_path);
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_HANDLE_TYPE:
    case PROP_HANDLE:
    case PROP_CHANNEL_TYPE:
      /* these properties are writable in the interface, but not actually
       * meaningfully changable on this channel, so we do nothing */
      break;
    case PROP_CONNECTION:
      priv->conn = g_value_get_object (value);
      break;
    case PROP_CREATOR:
      priv->creator = g_value_get_uint (value);
      break;
    case PROP_INITIAL_PEER:
      priv->initial_peer = g_value_get_uint (value);

      if (priv->initial_peer != 0)
        {
          TpBaseConnection *base_conn = (TpBaseConnection *) priv->conn;
          TpHandleRepoIface *repo = tp_base_connection_get_handles (base_conn,
              TP_HANDLE_TYPE_CONTACT);
          tp_handle_ref (repo, priv->initial_peer);
        }

      break;
    case PROP_MEDIA:
      g_assert (priv->media == NULL);
      priv->media = g_value_dup_object (value);
      break;
    case PROP_INITIAL_AUDIO:
      priv->initial_audio = g_value_get_boolean (value);
      break;
    case PROP_INITIAL_VIDEO:
      priv->initial_video = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

static void haze_media_channel_dispose (GObject *object);
static void haze_media_channel_finalize (GObject *object);

static void
haze_media_channel_class_init (HazeMediaChannelClass *haze_media_channel_class)
{
  static TpDBusPropertiesMixinPropImpl channel_props[] = {
      { "TargetHandleType", "handle-type", NULL },
      { "TargetHandle", "handle", NULL },
      { "TargetID", "target-id", NULL },
      { "ChannelType", "channel-type", NULL },
      { "Interfaces", "interfaces", NULL },
      { "Requested", "requested", NULL },
      { "InitiatorHandle", "creator", NULL },
      { "InitiatorID", "creator-id", NULL },
      { NULL }
  };
  static TpDBusPropertiesMixinPropImpl streamed_media_props[] = {
      { "InitialAudio", "initial-audio", NULL },
      { "InitialVideo", "initial-video", NULL },
      { NULL }
  };
  static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
      { TP_IFACE_CHANNEL,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        channel_props,
      },
      { TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
        tp_dbus_properties_mixin_getter_gobject_properties,
        NULL,
        streamed_media_props,
      },
      { NULL }
  };
  GObjectClass *object_class = G_OBJECT_CLASS (haze_media_channel_class);
  GParamSpec *param_spec;

  g_type_class_add_private (haze_media_channel_class,
      sizeof (HazeMediaChannelPrivate));

  object_class->constructor = haze_media_channel_constructor;

  object_class->get_property = haze_media_channel_get_property;
  object_class->set_property = haze_media_channel_set_property;

  object_class->dispose = haze_media_channel_dispose;
  object_class->finalize = haze_media_channel_finalize;

  g_object_class_override_property (object_class, PROP_OBJECT_PATH,
      "object-path");
  g_object_class_override_property (object_class, PROP_CHANNEL_TYPE,
      "channel-type");
  g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
      "handle-type");
  g_object_class_override_property (object_class, PROP_HANDLE, "handle");

  g_object_class_override_property (object_class, PROP_CHANNEL_DESTROYED,
      "channel-destroyed");
  g_object_class_override_property (object_class, PROP_CHANNEL_PROPERTIES,
      "channel-properties");

  param_spec = g_param_spec_string ("target-id", "Target ID",
      "The string that would result from inspecting TargetHandle",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TARGET_ID, param_spec);

  param_spec = g_param_spec_uint ("initial-peer", "Other participant",
      "The TpHandle representing the other participant in the channel if known "
      "at construct-time; 0 if the other participant was unknown at the time "
      "of channel creation",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIAL_PEER, param_spec);

  param_spec = g_param_spec_uint ("peer", "Other participant",
      "The TpHandle representing the other participant in the channel if "
      "currently known; 0 if this is an anonymous channel on which "
      "RequestStreams  has not yet been called.",
      0, G_MAXUINT32, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PEER, param_spec);

  param_spec = g_param_spec_object ("connection", "HazeConnection object",
      "Haze connection object that owns this media channel object.",
      HAZE_TYPE_CONNECTION,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

  param_spec = g_param_spec_uint ("creator", "Channel creator",
      "The TpHandle representing the contact who created the channel.",
      0, G_MAXUINT32, 0,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CREATOR, param_spec);

  param_spec = g_param_spec_string ("creator-id", "Creator ID",
      "The ID obtained by inspecting the creator handle.",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CREATOR_ID, param_spec);

  param_spec = g_param_spec_boolean ("requested", "Requested?",
      "True if this channel was requested by the local user",
      FALSE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_REQUESTED, param_spec);

  param_spec = g_param_spec_boxed ("interfaces", "Extra D-Bus interfaces",
      "Additional Channel.Interface.* interfaces",
      G_TYPE_STRV,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INTERFACES, param_spec);

  param_spec = g_param_spec_object ("media", "PurpleMedia object",
      "Purple media associated with this media channel object.",
      PURPLE_TYPE_MEDIA,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE |
      G_PARAM_STATIC_NAME | G_PARAM_STATIC_NICK | G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_MEDIA, param_spec);

  param_spec = g_param_spec_boolean ("initial-audio", "InitialAudio",
      "Whether the channel initially contained an audio stream",
      FALSE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIAL_AUDIO,
      param_spec);

  param_spec = g_param_spec_boolean ("initial-video", "InitialVideo",
      "Whether the channel initially contained an video stream",
      FALSE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIAL_VIDEO,
      param_spec);

  haze_media_channel_class->dbus_props_class.interfaces = prop_interfaces;
  tp_dbus_properties_mixin_class_init (object_class,
      G_STRUCT_OFFSET (HazeMediaChannelClass, dbus_props_class));

  tp_group_mixin_class_init (object_class,
      G_STRUCT_OFFSET (HazeMediaChannelClass, group_class),
      haze_media_channel_add_member, NULL);
  tp_group_mixin_class_set_remove_with_reason_func (object_class,
      haze_media_channel_remove_member);
  tp_group_mixin_class_allow_self_removal (object_class);

  tp_group_mixin_init_dbus_properties (object_class);
}

void
haze_media_channel_dispose (GObject *object)
{
  HazeMediaChannel *self = HAZE_MEDIA_CHANNEL (object);
  HazeMediaChannelPrivate *priv = self->priv;
  TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (
      conn, TP_HANDLE_TYPE_CONTACT);

  if (priv->dispose_has_run)
    return;

  DEBUG ("called");

  priv->dispose_has_run = TRUE;

  if (!priv->closed)
    haze_media_channel_close (self);

  g_assert (priv->closed);

  tp_handle_unref (contact_handles, priv->creator);
  priv->creator = 0;

  if (priv->initial_peer != 0)
    {
      tp_handle_unref (contact_handles, priv->initial_peer);
      priv->initial_peer = 0;
    }

  if (priv->media != NULL)
    g_object_unref (priv->media);
  priv->media = NULL;

  if (G_OBJECT_CLASS (haze_media_channel_parent_class)->dispose)
    G_OBJECT_CLASS (haze_media_channel_parent_class)->dispose (object);
}

void
haze_media_channel_finalize (GObject *object)
{
  HazeMediaChannel *self = HAZE_MEDIA_CHANNEL (object);
  HazeMediaChannelPrivate *priv = self->priv;

  g_free (priv->object_path);

  tp_group_mixin_finalize (object);

  G_OBJECT_CLASS (haze_media_channel_parent_class)->finalize (object);
}


/**
 * haze_media_channel_close_async:
 *
 * Implements D-Bus method Close
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
haze_media_channel_close_async (TpSvcChannel *iface,
                                DBusGMethodInvocation *context)
{
  HazeMediaChannel *self = HAZE_MEDIA_CHANNEL (iface);

  DEBUG ("called");
  haze_media_channel_close (self);
  tp_svc_channel_return_from_close (context);
}

void
haze_media_channel_close (HazeMediaChannel *self)
{
  HazeMediaChannelPrivate *priv = self->priv;

  DEBUG ("called on %p", self);

  if (!priv->closed)
    {
      priv->closed = TRUE;

      if (priv->media && !priv->media_ended)
        {
          priv->media_ended = TRUE;
          purple_media_stream_info (priv->media,
              PURPLE_MEDIA_INFO_HANGUP, NULL, NULL, FALSE);
        }

      tp_svc_channel_emit_closed (self);
    }
}


/**
 * haze_media_channel_get_channel_type
 *
 * Implements D-Bus method GetChannelType
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
haze_media_channel_get_channel_type (TpSvcChannel *iface,
                                     DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_channel_type (context,
      TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA);
}


/**
 * haze_media_channel_get_handle
 *
 * Implements D-Bus method GetHandle
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
haze_media_channel_get_handle (TpSvcChannel *iface,
                               DBusGMethodInvocation *context)
{
  HazeMediaChannel *self = HAZE_MEDIA_CHANNEL (iface);

  if (self->priv->initial_peer == 0)
    tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_NONE, 0);
  else
    tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_CONTACT,
        self->priv->initial_peer);
}


/**
 * haze_media_channel_get_interfaces
 *
 * Implements D-Bus method GetInterfaces
 * on interface org.freedesktop.Telepathy.Channel
 */
static void
haze_media_channel_get_interfaces (TpSvcChannel *iface,
                                   DBusGMethodInvocation *context)
{
  tp_svc_channel_return_from_get_interfaces (context,
      haze_media_channel_interfaces);
}

/**
 * haze_media_channel_list_streams
 *
 * Implements D-Bus method ListStreams
 * on interface org.freedesktop.Telepathy.Channel.Type.StreamedMedia
 */
static void
haze_media_channel_list_streams (TpSvcChannelTypeStreamedMedia *iface,
                                 DBusGMethodInvocation *context)
{
  HazeMediaChannel *self = HAZE_MEDIA_CHANNEL (iface);
  HazeMediaChannelPrivate *priv;
  GPtrArray *ret;

  g_assert (HAZE_IS_MEDIA_CHANNEL (self));

  priv = self->priv;

  /* If the session has not yet started, return an empty array. */
  if (priv->media == NULL)
    {
      ret = g_ptr_array_new ();
    }
  else
    {
      HazeMediaBackend *backend;
      GPtrArray *streams;

      g_object_get (G_OBJECT (priv->media), "backend", &backend, NULL);
      g_object_get (G_OBJECT (backend), "streams", &streams, NULL);

      ret = make_stream_list (self, streams->len,
          (HazeMediaStream **) streams->pdata);

      g_ptr_array_unref (streams);
      g_object_unref (backend);
    }

  tp_svc_channel_type_streamed_media_return_from_list_streams (context, ret);
  g_ptr_array_foreach (ret, (GFunc) g_value_array_free, NULL);
  g_ptr_array_free (ret, TRUE);
}

/**
 * haze_media_channel_remove_streams
 *
 * Implements DBus method RemoveStreams
 * on interface org.freedesktop.Telepathy.Channel.Type.StreamedMedia
 */
static void
haze_media_channel_remove_streams (TpSvcChannelTypeStreamedMedia *iface,
                                   const GArray * streams,
                                   DBusGMethodInvocation *context)
{
  HazeMediaChannel *obj = HAZE_MEDIA_CHANNEL (iface);
  HazeMediaChannelPrivate *priv;
  HazeMediaBackend *backend;
  GPtrArray *backend_streams;
  guint i, j;
  GPtrArray *media_ids;
  const gchar *target_id;

  g_assert (HAZE_IS_MEDIA_CHANNEL (obj));

  priv = obj->priv;

  g_object_get (obj, "target-id", &target_id, NULL);

  if ((purple_prpl_get_media_caps (priv->conn->account, target_id) &
      PURPLE_MEDIA_CAPS_MODIFY_SESSION) == 0)
    {
      TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->conn);
      gchar *name;
      GError *e;

      g_object_get (base_conn, "protocol", &name, NULL);
      g_set_error (&e, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
          "Streams can't be removed in Haze's \"%s\" protocol's calls", name);
      g_free (name);

      DEBUG ("%s", e->message);
      dbus_g_method_return_error (context, e);

      g_error_free(e);
      return;
    }

  media_ids = g_ptr_array_new ();

  g_object_get (G_OBJECT (priv->media), "backend", &backend, NULL);
  g_object_get (G_OBJECT (backend), "streams", &backend_streams, NULL);
  g_object_unref (backend);

  for (i = 0; i < streams->len; ++i)
    {
      guint id = g_array_index (streams, guint, i);

      for (j = 0; j < backend_streams->len; j++)
        {
          HazeMediaStream *stream = g_ptr_array_index (backend_streams, j);
          guint stream_id;

          g_object_get (G_OBJECT (stream), "id", &stream_id, NULL);

          if (id == stream_id)
            {
              g_ptr_array_add (media_ids, stream->name);
              break;
            }
        }

      if (j >= backend_streams->len)
        {
          GError e = { TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
              "Requested stream wasn't found" };
          DEBUG ("%s", e.message);
          dbus_g_method_return_error (context, &e);
          g_ptr_array_free (media_ids, TRUE);
          return;
        }
    }

  for (i = 0; i < media_ids->len; ++i)
    {
      gchar *id = g_ptr_array_index (media_ids, i);
      for (j = i + 1; j < media_ids->len; ++j)
        {
          if (id == g_ptr_array_index (media_ids, j))
            {
              g_ptr_array_remove_index (media_ids, j);
              --j;
            }
        }
    }

  for (i = 0; i < media_ids->len; ++i)
    {
      purple_media_end (priv->media, g_ptr_array_index (media_ids, i), NULL);
    }

  g_ptr_array_unref (backend_streams);
  g_ptr_array_free (media_ids, TRUE);
  tp_svc_channel_type_streamed_media_return_from_remove_streams (context);
}

/**
 * haze_media_channel_request_stream_direction
 *
 * Implements D-Bus method RequestStreamDirection
 * on interface org.freedesktop.Telepathy.Channel.Type.StreamedMedia
 */
static void
haze_media_channel_request_stream_direction (TpSvcChannelTypeStreamedMedia *iface,
                                             guint stream_id,
                                             guint stream_direction,
                                             DBusGMethodInvocation *context)
{
  /* Libpurple doesn't have API for this yet */
  GError e = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
      "Stream direction can't be set Haze calls" };
  DEBUG ("%s", e.message);
  dbus_g_method_return_error (context, &e);
}


static gboolean
init_media_cb (PurpleMediaManager *manager,
               PurpleMedia *media,
               PurpleAccount *account,
               const gchar *username,
               HazeMediaChannel *self)
{
  HazeMediaChannelPrivate *priv = self->priv;
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->conn);
  TpHandleRepoIface *contact_repo =
      tp_base_connection_get_handles (base_conn, TP_HANDLE_TYPE_CONTACT);
  TpHandle contact = tp_handle_ensure (contact_repo, username, NULL, NULL);

  if (priv->conn->account != account || priv->initial_peer != contact)
    return TRUE;

  g_assert (priv->media == NULL);
  priv->media = g_object_ref (media);
  if (priv->media != NULL)
    {
      _latch_to_session (self);
    }

  g_signal_handlers_disconnect_by_func (manager, init_media_cb, self);

  return TRUE;
}

static gboolean
_haze_media_channel_request_contents (HazeMediaChannel *chan,
                                      TpHandle peer,
                                      const GArray *media_types,
                                      GError **error)
{
  HazeMediaChannelPrivate *priv = chan->priv;
  gboolean want_audio, want_video;
  guint idx;
  TpHandleRepoIface *contact_handles;
  const gchar *contact_id;
  guint audio_count = 0, video_count = 0;

  DEBUG ("called");

  want_audio = want_video = FALSE;

  for (idx = 0; idx < media_types->len; idx++)
    {
      guint media_type = g_array_index (media_types, guint, idx);

      if (media_type == TP_MEDIA_STREAM_TYPE_AUDIO)
        {
          want_audio = TRUE;
        }
      else if (media_type == TP_MEDIA_STREAM_TYPE_VIDEO)
        {
          want_video = TRUE;
        }
      else
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
              "given media type %u is invalid", media_type);
          return FALSE;
        }
    }

  /* existing call; the recipient and the mode has already been decided */
  if (priv->media != NULL)
    {
      PurpleMediaCaps caps;
      const gchar *target_id;
      g_object_get (chan, "target-id", &target_id, NULL);
      caps = purple_prpl_get_media_caps (priv->conn->account, target_id);

      /* Check if the contact supports modifying the session */
      if ((caps & PURPLE_MEDIA_CAPS_MODIFY_SESSION) == 0)
        {
          TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->conn);
          gchar *name;

          g_object_get (base_conn, "protocol", &name, NULL);
          g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "Streams can't be added in Haze's \"%s\" protocol's calls",
              name);

          g_free (name);
          return FALSE;
        }

      /* Check if contact supports the desired media type */
      if ((want_audio == FALSE || caps & PURPLE_MEDIA_CAPS_AUDIO) &&
          (want_video == FALSE || caps & PURPLE_MEDIA_CAPS_VIDEO))
        {
          g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "Member does not have the desired audio/video capabilities");
          return FALSE;
        }
    }

  /* if we've got here, we're good to make the streams */

  contact_handles = tp_base_connection_get_handles (
      TP_BASE_CONNECTION (priv->conn), TP_HANDLE_TYPE_CONTACT);
  contact_id = tp_handle_inspect (contact_handles, peer);

  /* Be ready to retrieve the newly created media object */
  if (priv->media == NULL)
      g_signal_connect (purple_media_manager_get (), "init-media",
          G_CALLBACK (init_media_cb), chan);

  for (idx = 0; idx < media_types->len; idx++)
    {
      guint media_type = g_array_index (media_types, guint, idx);

      if (media_type == TP_MEDIA_STREAM_TYPE_AUDIO)
        ++audio_count;
      else if (media_type == TP_MEDIA_STREAM_TYPE_VIDEO)
        ++video_count;
    }

  while (audio_count > 0 || video_count > 0)
    {
      PurpleMediaSessionType type = PURPLE_MEDIA_NONE;

      if (audio_count > 0)
        {
          type |= PURPLE_MEDIA_AUDIO;
          --audio_count;
        }

      if (video_count > 0)
        {
          type |= PURPLE_MEDIA_VIDEO;
          --video_count;
        }

      if (purple_prpl_initiate_media (priv->conn->account,
          contact_id, type) == FALSE)
        return FALSE;
    }

  return TRUE;
}

static void
media_channel_request_streams (HazeMediaChannel *self,
    TpHandle contact_handle,
    const GArray *types,
    GFunc succeeded_cb,
    GFunc failed_cb,
    gpointer context)
{
  HazeMediaChannelPrivate *priv = self->priv;
  PendingStreamRequest *psr = NULL;
  GError *error = NULL;

  if (types->len == 0)
    {
      GPtrArray *empty = g_ptr_array_sized_new (0);

      DEBUG ("no streams to request");
      succeeded_cb (context, empty);
      g_ptr_array_free (empty, TRUE);

      return;
    }

  if (priv->media != NULL)
    {
      TpHandle peer;

      peer = priv->initial_peer;

      if (peer != contact_handle)
        {
          g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
              "cannot add streams for %u: this channel's peer is %u",
              contact_handle, peer);
          goto error;
        }
    }

  /*
   * Pending stream requests can be completed before request_contents returns.
   * Add the pending stream request up here so it isn't missed.
   */
  psr = pending_stream_request_new (types, succeeded_cb, failed_cb,
      context);
  priv->pending_stream_requests = g_list_prepend (priv->pending_stream_requests,
      psr);

  if (!_haze_media_channel_request_contents (self, contact_handle,
      types, &error))
    goto error;

  return;

error:
  if (psr != NULL)
    {
      priv->pending_stream_requests = g_list_remove (
          priv->pending_stream_requests, psr);
      pending_stream_request_free (psr);
    }

  DEBUG ("returning error %u: %s", error->code, error->message);
  failed_cb (context, error);
  g_error_free (error);
}

/**
 * haze_media_channel_request_streams
 *
 * Implements D-Bus method RequestStreams
 * on interface org.freedesktop.Telepathy.Channel.Type.StreamedMedia
 */
static void
haze_media_channel_request_streams (TpSvcChannelTypeStreamedMedia *iface,
                                    guint contact_handle,
                                    const GArray *types,
                                    DBusGMethodInvocation *context)
{
  HazeMediaChannel *self = HAZE_MEDIA_CHANNEL (iface);
  TpBaseConnection *base_conn = (TpBaseConnection *) self->priv->conn;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (
      base_conn, TP_HANDLE_TYPE_CONTACT);
  GError *error = NULL;

  if (!tp_handle_is_valid (contact_handles, contact_handle, &error))
    {
      DEBUG ("that's not a handle, sonny! (%u)", contact_handle);
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }
  else
    {
      /* FIXME: disallow this if we've put the peer on hold? */

      media_channel_request_streams (self, contact_handle, types,
          (GFunc) tp_svc_channel_type_streamed_media_return_from_request_streams,
          (GFunc) dbus_g_method_return_error,
          context);
    }
}

/**
 * haze_media_channel_request_initial_streams:
 * @chan: an outgoing call, which must have just been constructed.
 * @succeeded_cb: called with arguments @user_data and a GPtrArray of
 *                TP_STRUCT_TYPE_MEDIA_STREAM_INFO if the request succeeds.
 * @failed_cb: called with arguments @user_data and a GError * if the request
 *             fails.
 * @user_data: context for the callbacks.
 *
 * Request streams corresponding to the values of InitialAudio and InitialVideo
 * in the channel request.
 */
void
haze_media_channel_request_initial_streams (HazeMediaChannel *chan,
    GFunc succeeded_cb,
    GFunc failed_cb,
    gpointer user_data)
{
  HazeMediaChannelPrivate *priv = chan->priv;
  GArray *types = g_array_sized_new (FALSE, FALSE, sizeof (guint), 2);
  guint media_type;

  /* This has to be an outgoing call... */
  g_assert (priv->creator == priv->conn->parent.self_handle);

  if (priv->initial_audio)
    {
      media_type = TP_MEDIA_STREAM_TYPE_AUDIO;
      g_array_append_val (types, media_type);
    }

  if (priv->initial_video)
    {
      media_type = TP_MEDIA_STREAM_TYPE_VIDEO;
      g_array_append_val (types, media_type);
    }

  media_channel_request_streams (chan, priv->initial_peer, types,
      succeeded_cb, failed_cb, user_data);

  g_array_free (types, TRUE);
}

static gboolean
haze_media_channel_add_member (GObject *obj,
    TpHandle handle,
    const gchar *message,
    GError **error)
{
  HazeMediaChannel *chan = HAZE_MEDIA_CHANNEL (obj);
  HazeMediaChannelPrivate *priv = chan->priv;
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);
  TpIntSet *set;

  /* did we create this channel? */
  if (priv->creator == mixin->self_handle)
    {
      /* yes: check we don't have a peer already, and if not add this one to
       * remote pending (but don't send an invitation yet).
       */
      if (priv->media != NULL)
        {
          TpHandle peer;

          peer = priv->initial_peer;

          if (peer != handle)
            {
              g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                  "handle %u cannot be added: this channel's peer is %u",
                  handle, peer);
              return FALSE;
            }
        }

      /* make the peer remote pending */
      set = tp_intset_new_containing (handle);
      tp_group_mixin_change_members (obj, "", NULL, NULL, NULL, set,
          mixin->self_handle, TP_CHANNEL_GROUP_CHANGE_REASON_INVITED);
      tp_intset_destroy (set);

      /* and remove CanAdd, since it was only here to allow this deprecated
       * API. */
      tp_group_mixin_change_flags (obj, 0, TP_CHANNEL_GROUP_FLAG_CAN_ADD);

      return TRUE;
    }
  else
    {
      /* no: has a session been created, is the handle being added ours,
       *     and are we in local pending? (call answer) */
      if (priv->media &&
          handle == mixin->self_handle &&
          tp_handle_set_is_member (mixin->local_pending, handle))
        {
          /* is the call on hold? */
          if (priv->hold_state != TP_LOCAL_HOLD_STATE_UNHELD)
            {
              g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                  "Can't answer a call while it's on hold");
              return FALSE;
            }

          /* make us a member */
          set = tp_intset_new_containing (handle);
          tp_group_mixin_change_members (obj, "", set, NULL, NULL, NULL,
              handle, TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
          tp_intset_destroy (set);

          /* signal acceptance */
          purple_media_stream_info(priv->media,
              PURPLE_MEDIA_INFO_ACCEPT, NULL, NULL, TRUE);

          return TRUE;
        }
    }

  g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
      "handle %u cannot be added in the current state", handle);
  return FALSE;
}

static gboolean
haze_media_channel_remove_member (GObject *obj,
                                  TpHandle handle,
                                  const gchar *message,
                                  guint reason,
                                  GError **error)
{
  HazeMediaChannel *chan = HAZE_MEDIA_CHANNEL (obj);
  HazeMediaChannelPrivate *priv = chan->priv;
  TpGroupMixin *mixin = TP_GROUP_MIXIN (obj);

  /* We don't set CanRemove, and did allow self removal. So tp-glib should
   * ensure this.
   */
  g_assert (handle == mixin->self_handle);

  /* Closing up might make HazeMediaManager release its ref. */
  g_object_ref (chan);

  if (priv->media == NULL)
    {
      haze_media_channel_close (chan);
    }
  else
    {
      switch (reason)
        {
        /* Should one of these trigger reject? */
        case TP_CHANNEL_GROUP_CHANGE_REASON_NONE:
        case TP_CHANNEL_GROUP_CHANGE_REASON_OFFLINE:
        case TP_CHANNEL_GROUP_CHANGE_REASON_BUSY:
        case TP_CHANNEL_GROUP_CHANGE_REASON_ERROR:
        case TP_CHANNEL_GROUP_CHANGE_REASON_NO_ANSWER:
          purple_media_stream_info(priv->media,
              PURPLE_MEDIA_INFO_HANGUP, NULL, NULL, TRUE);
          break;
        default:
          /* The remaining options don't make sense */
          g_object_unref (chan);
          return FALSE;
       }
    }

  /* Remove CanAdd if it was there for the deprecated anonymous channel
   * semantics, since the channel will go away RSN. */
  tp_group_mixin_change_flags (obj, 0, TP_CHANNEL_GROUP_FLAG_CAN_ADD);

  g_object_unref (chan);

  return TRUE;
}

/**
 * haze_media_channel_get_session_handlers
 *
 * Implements D-Bus method GetSessionHandlers
 * on interface org.freedesktop.Telepathy.Channel.Interface.MediaSignalling
 */
static void
haze_media_channel_get_session_handlers (
    TpSvcChannelInterfaceMediaSignalling *iface,
    DBusGMethodInvocation *context)
{
  HazeMediaChannel *self = HAZE_MEDIA_CHANNEL (iface);
  HazeMediaChannelPrivate *priv;
  GPtrArray *ret;
  GType info_type = TP_STRUCT_TYPE_MEDIA_SESSION_HANDLER_INFO;

  g_assert (HAZE_IS_MEDIA_CHANNEL (self));

  priv = self->priv;

  if (priv->media)
    {
      GValue handler = { 0, };
      HazeMediaBackend *backend;
      gchar *object_path;

      g_object_get (G_OBJECT (priv->media), "backend", &backend, NULL);
      g_object_get (G_OBJECT (backend), "object-path", &object_path, NULL);
      g_object_unref (backend);

      g_value_init (&handler, info_type);
      g_value_take_boxed (&handler,
          dbus_g_type_specialized_construct (info_type));

      dbus_g_type_struct_set (&handler,
          0, object_path,
          1, "rtp",
          G_MAXUINT);

      g_free (object_path);

      ret = g_ptr_array_sized_new (1);
      g_ptr_array_add (ret, g_value_get_boxed (&handler));
    }
  else
    {
      ret = g_ptr_array_sized_new (0);
    }

  tp_svc_channel_interface_media_signalling_return_from_get_session_handlers (
      context, ret);
  g_ptr_array_foreach (ret, (GFunc) g_value_array_free, NULL);
  g_ptr_array_free (ret, TRUE);
}

static void
channel_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *) g_iface;

#define IMPLEMENT(x, suffix) tp_svc_channel_implement_##x (\
    klass, haze_media_channel_##x##suffix)
  IMPLEMENT(close,_async);
  IMPLEMENT(get_channel_type,);
  IMPLEMENT(get_handle,);
  IMPLEMENT(get_interfaces,);
#undef IMPLEMENT
}

static void
streamed_media_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelTypeStreamedMediaClass *klass =
    (TpSvcChannelTypeStreamedMediaClass *) g_iface;

#define IMPLEMENT(x) tp_svc_channel_type_streamed_media_implement_##x (\
    klass, haze_media_channel_##x)
  IMPLEMENT(list_streams);
  IMPLEMENT(remove_streams);
  IMPLEMENT(request_stream_direction);
  IMPLEMENT(request_streams);
#undef IMPLEMENT
}

static void
media_signalling_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelInterfaceMediaSignallingClass *klass =
    (TpSvcChannelInterfaceMediaSignallingClass *) g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_media_signalling_implement_##x (\
    klass, haze_media_channel_##x)
  IMPLEMENT(get_session_handlers);
#undef IMPLEMENT
}
