/*
 * media-backend.c - Source for HazeMediaBackend
 * Copyright © 2006-2009 Collabora Ltd.
 * Copyright © 2006-2009 Nokia Corporation
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
#include "media-backend.h"

#include <libpurple/media/backend-iface.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/svc-media-interfaces.h>

#include <string.h>

#include "debug.h"

static void media_backend_iface_init(PurpleMediaBackendIface *iface);
static void session_handler_iface_init (gpointer g_iface,
                                        gpointer iface_data);
static void haze_backend_state_changed_cb (PurpleMedia *media,
                                           PurpleMediaState state,
                                           const gchar *sid,
                                           const gchar *name,
                                           HazeMediaBackend *backend);

G_DEFINE_TYPE_WITH_CODE (HazeMediaBackend,
    haze_media_backend,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (PURPLE_TYPE_MEDIA_BACKEND,
      media_backend_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_MEDIA_SESSION_HANDLER,
      session_handler_iface_init);
    )

/* properties */
enum
{
  PROP_CONFERENCE_TYPE = 1,
  PROP_MEDIA,
  PROP_OBJECT_PATH,
  PROP_STREAMS,
  LAST_PROPERTY
};

/* private structure */
struct _HazeMediaBackendPrivate
{
  gchar *conference_type;
  gchar *object_path;
  gpointer media;
  GPtrArray *streams;

  guint next_stream_id;
  gboolean ready;
};

static void
haze_media_backend_init (HazeMediaBackend *self)
{
  HazeMediaBackendPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      HAZE_TYPE_MEDIA_BACKEND, HazeMediaBackendPrivate);

  self->priv = priv;

  priv->next_stream_id = 1;
  priv->streams = g_ptr_array_sized_new (1);
}

static void
haze_media_backend_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  HazeMediaBackend *backend = HAZE_MEDIA_BACKEND (object);
  HazeMediaBackendPrivate *priv = backend->priv;

  switch (property_id)
    {
    case PROP_CONFERENCE_TYPE:
      g_value_set_string (value, priv->conference_type);
      break;
    case PROP_MEDIA:
      g_value_set_object (value, priv->media);
      break;
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_STREAMS:
      g_value_set_boxed (value, priv->streams);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
haze_media_backend_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  HazeMediaBackend *backend = HAZE_MEDIA_BACKEND (object);
  HazeMediaBackendPrivate *priv = backend->priv;

  switch (property_id)
    {
    case PROP_CONFERENCE_TYPE:
      g_free (priv->conference_type);
      priv->conference_type = g_value_dup_string (value);
      break;
    case PROP_MEDIA:
      g_assert (priv->media == NULL);
      priv->media = g_value_get_object (value);

      g_object_add_weak_pointer(G_OBJECT(priv->media), &priv->media);
      g_signal_connect (priv->media, "state-changed",
          G_CALLBACK (haze_backend_state_changed_cb), backend);
      break;
    case PROP_OBJECT_PATH:
      g_assert (priv->object_path == NULL);
      priv->object_path = g_value_dup_string (value);

      if (priv->object_path != NULL)
        {
          TpDBusDaemon *dbus_daemon = tp_dbus_daemon_dup (NULL);

          g_return_if_fail (dbus_daemon != NULL);
          tp_dbus_daemon_register_object (dbus_daemon,
              priv->object_path, G_OBJECT (backend));
          g_object_unref (dbus_daemon);
        }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void haze_media_backend_dispose (GObject *object);
static void haze_media_backend_finalize (GObject *object);

static void
haze_media_backend_class_init (HazeMediaBackendClass *haze_media_backend_class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (haze_media_backend_class);
  GParamSpec *param_spec;

  g_type_class_add_private (haze_media_backend_class,
      sizeof (HazeMediaBackendPrivate));

  object_class->get_property = haze_media_backend_get_property;
  object_class->set_property = haze_media_backend_set_property;

  object_class->dispose = haze_media_backend_dispose;
  object_class->finalize = haze_media_backend_finalize;

  g_object_class_override_property(object_class, PROP_CONFERENCE_TYPE,
      "conference-type");
  g_object_class_override_property(object_class, PROP_MEDIA, "media");

  param_spec = g_param_spec_string ("object-path", "D-Bus object path",
                                    "The D-Bus object path used for this "
                                    "object on the bus.",
                                    NULL,
                                    G_PARAM_READWRITE |
                                    G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_OBJECT_PATH, param_spec);

  param_spec = g_param_spec_boxed ("streams", "Streams",
                                   "List of streams handled by this backend.",
                                   G_TYPE_PTR_ARRAY,
                                   G_PARAM_READABLE |
                                   G_PARAM_STATIC_NAME |
                                   G_PARAM_STATIC_BLURB);
  g_object_class_install_property (object_class, PROP_STREAMS, param_spec);
}

void
haze_media_backend_dispose (GObject *object)
{
  DEBUG ("called");

  if (G_OBJECT_CLASS (haze_media_backend_parent_class)->dispose)
    G_OBJECT_CLASS (haze_media_backend_parent_class)->dispose (object);
}

void
haze_media_backend_finalize (GObject *object)
{
  HazeMediaBackend *self = HAZE_MEDIA_BACKEND (object);
  HazeMediaBackendPrivate *priv = self->priv;

  g_free (priv->conference_type);
  g_free (priv->object_path);

  if (priv->streams != NULL)
    g_ptr_array_free (priv->streams, TRUE);

  G_OBJECT_CLASS (haze_media_backend_parent_class)->finalize (object);
}

static HazeMediaStream *
get_stream_by_name (HazeMediaBackend *self,
                    const gchar *sid)
{
  HazeMediaBackendPrivate *priv = self->priv;
  guint i;

  for (i = 0; i < priv->streams->len; ++i)
    {
      HazeMediaStream *stream = g_ptr_array_index (priv->streams, i);

      if (!strcmp (sid, stream->name))
        return stream;
    }

  return NULL;
}

HazeMediaStream *
haze_media_backend_get_stream_by_name (HazeMediaBackend *self,
                    const gchar *sid)
{
  return get_stream_by_name (self, sid);
}

static void
haze_backend_state_changed_cb (PurpleMedia *media,
                               PurpleMediaState state,
                               const gchar *sid,
                               const gchar *name,
                               HazeMediaBackend *backend)
{
  HazeMediaBackendPrivate *priv = backend->priv;

  if (state == PURPLE_MEDIA_STATE_END && sid != NULL && name == NULL)
    {
      HazeMediaStream *stream = get_stream_by_name (backend, sid);

      if (stream != NULL)
        {
          g_ptr_array_remove_fast (priv->streams, stream);
          g_object_unref (stream);
        }
    }
}

static void
_emit_new_stream (HazeMediaBackend *self,
                  HazeMediaStream *stream)
{
  gchar *object_path;
  guint id, media_type;

  g_object_get (stream,
                "object-path", &object_path,
                "id", &id,
                "media-type", &media_type,
                NULL);

  /* all of the streams are bidirectional from farsight's point of view, it's
   * just in the signalling they change */
  DEBUG ("emitting MediaSessionHandler:NewStreamHandler signal for %s stream %d",
      media_type == TP_MEDIA_STREAM_TYPE_AUDIO ? "audio" : "video", id);
  tp_svc_media_session_handler_emit_new_stream_handler (self,
      object_path, id, media_type, TP_MEDIA_STREAM_DIRECTION_BIDIRECTIONAL);

  g_free (object_path);
}

static gboolean
haze_media_backend_add_stream (PurpleMediaBackend *self,
    const gchar *sid, const gchar *who,
    PurpleMediaSessionType type, gboolean initiator,
    const gchar *transmitter,
    guint num_params, GParameter *params)
{
  HazeMediaBackendPrivate *priv = HAZE_MEDIA_BACKEND (self)->priv;
  HazeMediaStream *stream;
  gchar *object_path;
  guint media_type, id, stun_port = 3478; /* default stun port */
  const gchar *nat_traversal = NULL, *stun_server = NULL;
  TpDBusDaemon *dbus_daemon = tp_dbus_daemon_dup (NULL);

  DEBUG ("called");

  g_return_val_if_fail (dbus_daemon != NULL, FALSE);

  id = priv->next_stream_id++;

  object_path = g_strdup_printf ("%s/MediaStream%u",
      priv->object_path, id);

  if (type & PURPLE_MEDIA_AUDIO)
    media_type = TP_MEDIA_STREAM_TYPE_AUDIO;
  else
    media_type = TP_MEDIA_STREAM_TYPE_VIDEO;

  if (!strcmp (transmitter, "nice"))
    {
      guint i;

      for (i = 0; i < num_params; ++i)
        {
          if (!strcmp (params[i].name, "compatibility-mode") &&
              G_VALUE_HOLDS (&params[i].value, G_TYPE_UINT))
            {
              guint mode = g_value_get_uint (&params[i].value);

              switch (mode)
                {
                case 0: /* NICE_COMPATIBILITY_DRAFT19 */
                  nat_traversal = "ice-udp";
                  break;
                case 1: /* NICE_COMPATIBILITY_GOOGLE */
                  nat_traversal = "gtalk-p2p";
                  break;
                case 2: /* NICE_COMPATIBILITY_MSN */
                  nat_traversal = "wlm-8.5";
                  break;
                case 3: /* NICE_COMPATIBILITY_WLM2009 */
                  nat_traversal = "wlm-2009";
                  break;
                default:
                  g_assert_not_reached ();
                }
            }
          else if (!strcmp (params[i].name, "stun-ip") &&
              G_VALUE_HOLDS (&params[i].value, G_TYPE_STRING))
            {
              stun_server = g_value_get_string (&params[i].value);
            }
          else if (!strcmp (params[i].name, "stun-port") &&
              G_VALUE_HOLDS (&params[i].value, G_TYPE_UINT))
            {
              stun_port = g_value_get_uint (&params[i].value);
            }
        }

      if (nat_traversal == NULL)
        nat_traversal = "ice-udp";
    }
  else if (!strcmp (transmitter, "rawudp"))
    {
      nat_traversal = "none";
    }
  else
    {
      g_assert_not_reached ();
    }

  stream = haze_media_stream_new (object_path, dbus_daemon, priv->media,
      sid, who, media_type, id, initiator, nat_traversal, NULL, FALSE);

  if (stun_server != NULL)
    haze_media_stream_add_stun_server (stream, stun_server, stun_port);

  g_free (object_path);

  DEBUG ("%p: created new MediaStream %p for sid '%s'",
      self, stream, sid);

  g_ptr_array_add (priv->streams, stream);

  if (priv->ready)
      _emit_new_stream (HAZE_MEDIA_BACKEND (self), stream);

  g_object_unref (dbus_daemon);

  return TRUE;
}

static void
haze_media_backend_add_remote_candidates (PurpleMediaBackend *self,
                                          const gchar *sid,
                                          const gchar *who,
                                          GList *remote_candidates)
{
  HazeMediaStream *stream;

  DEBUG ("called");

  stream = get_stream_by_name (HAZE_MEDIA_BACKEND (self), sid);

  if (stream != NULL)
    haze_media_stream_add_remote_candidates (stream, remote_candidates);
  else
    DEBUG ("Couldn't find stream");
}

static gboolean
haze_media_backend_codecs_ready (PurpleMediaBackend *self,
                                 const gchar *sid)
{
  HazeMediaStream *stream;
  gboolean ready = FALSE;

  DEBUG ("called");

  if (sid != NULL)
    {
      stream = get_stream_by_name (HAZE_MEDIA_BACKEND (self), sid);

      if (stream != NULL)
        g_object_get (stream, "codecs-ready", &ready, NULL);

      return ready;
    }
  else
    {
      HazeMediaBackendPrivate *priv = HAZE_MEDIA_BACKEND (self)->priv;
      guint i;

      for (i = 0; i < priv->streams->len; ++i)
        {
          stream = g_ptr_array_index (priv->streams, i);

          if (stream != NULL)
            g_object_get (stream, "codecs-ready", &ready, NULL);

          if (!ready)
            return FALSE;
        }

      return TRUE;
    }
}

static GList *
haze_media_backend_get_codecs (PurpleMediaBackend *self,
                               const gchar *sid)
{
  HazeMediaStream *stream;
  GList *ret = NULL;

  DEBUG ("called");

  stream = get_stream_by_name (HAZE_MEDIA_BACKEND (self), sid);

  if (stream != NULL)
    ret = haze_media_stream_get_codecs (stream);

  return ret;
}

static GList *
haze_media_backend_get_local_candidates (PurpleMediaBackend *self,
                                         const gchar *sid,
                                         const gchar *who)
{
  HazeMediaStream *stream;
  GList *ret = NULL;

  DEBUG ("called");

  stream = get_stream_by_name (HAZE_MEDIA_BACKEND (self), sid);

  if (stream != NULL)
    ret = haze_media_stream_get_local_candidates (stream);

  return ret;
}

static gboolean
haze_media_backend_set_remote_codecs (PurpleMediaBackend *self,
                                      const gchar *sid,
                                      const gchar *who,
                                      GList *codecs)
{
  HazeMediaStream *stream;

  DEBUG ("called");

  stream = get_stream_by_name (HAZE_MEDIA_BACKEND (self), sid);

  if (stream != NULL)
    haze_media_stream_set_remote_codecs (stream, codecs);
  else
    DEBUG ("Couldn't find stream");

  return TRUE;
}

static gboolean
haze_media_backend_set_send_codec (PurpleMediaBackend *self,
                                   const gchar *sid,
                                   PurpleMediaCodec *codec)
{
  return FALSE;
}

static void
haze_media_backend_ready (TpSvcMediaSessionHandler *iface,
                          DBusGMethodInvocation *context)
{
  HazeMediaBackend *self = HAZE_MEDIA_BACKEND (iface);
  HazeMediaBackendPrivate *priv = self->priv;

  if (!priv->ready)
    {
      guint i;

      DEBUG ("emitting NewStreamHandler for each stream");

      priv->ready = TRUE;

      for (i = 0; i < priv->streams->len; i++)
        _emit_new_stream (self, g_ptr_array_index (priv->streams, i));
    }

  tp_svc_media_session_handler_return_from_ready (context);
}

static void
haze_media_backend_error (TpSvcMediaSessionHandler *iface,
                          guint errno,
                          const gchar *message,
                          DBusGMethodInvocation *context)
{
  HazeMediaBackend *self = HAZE_MEDIA_BACKEND (iface);
  HazeMediaBackendPrivate *priv;
  GPtrArray *tmp;
  guint i;

  g_assert (HAZE_IS_MEDIA_BACKEND (self));

  priv = self->priv;

  if (priv->media == NULL)
    {
      /* This could also be because someone called Error() before the
       * SessionHandler was announced. But the fact that the SessionHandler is
       * actually also the Channel, and thus this method is available before
       * NewSessionHandler is emitted, is an implementation detail. So the
       * error message describes the only legitimate situation in which this
       * could arise.
       */
      GError e = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "call has already ended" };

      DEBUG ("no session, returning an error.");
      dbus_g_method_return_error (context, &e);
      return;
    }

  DEBUG ("Media.SessionHandler::Error called, error %u (%s) -- "
      "emitting error on each stream", errno, message);

  purple_media_end (priv->media, NULL, NULL);

  /* Calling haze_media_stream_error () on all the streams will ultimately
   * cause them all to emit 'closed'. In response to 'closed', stream_close_cb
   * unrefs them, and removes them from priv->streams. So, we copy the stream
   * list to avoid it being modified from underneath us.
   */
  tmp = g_ptr_array_sized_new (priv->streams->len);

  for (i = 0; i < priv->streams->len; i++)
    g_ptr_array_add (tmp, g_ptr_array_index (priv->streams, i));

  for (i = 0; i < tmp->len; i++)
    {
      HazeMediaStream *stream = g_ptr_array_index (tmp, i);

      haze_media_stream_error (stream, errno, message, NULL);
    }

  g_ptr_array_free (tmp, TRUE);

  tp_svc_media_session_handler_return_from_error (context);
}

static void
media_backend_iface_init (PurpleMediaBackendIface *iface)
{
#define IMPLEMENT(x) iface->x = haze_media_backend_##x
  IMPLEMENT(add_stream);
  IMPLEMENT(add_remote_candidates);
  IMPLEMENT(codecs_ready);
  IMPLEMENT(get_codecs);
  IMPLEMENT(get_local_candidates);
  IMPLEMENT(set_remote_codecs);
  IMPLEMENT(set_send_codec);
#undef IMPLEMENT
}

static void
session_handler_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcMediaSessionHandlerClass *klass =
    (TpSvcMediaSessionHandlerClass *) g_iface;

#define IMPLEMENT(x) tp_svc_media_session_handler_implement_##x (\
    klass, haze_media_backend_##x)
  IMPLEMENT(error);
  IMPLEMENT(ready);
#undef IMPLEMENT
}
