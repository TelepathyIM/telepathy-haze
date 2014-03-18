/*
 * im-channel.c - HazeIMChannel source
 * Copyright (C) 2007 Will Thompson
 * Copyright (C) 2007-2008 Collabora Ltd.
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

#include <config.h>
#include "im-channel.h"

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "connection.h"
#include "debug.h"

struct _HazeIMChannelPrivate
{
    PurpleConversation *conv;
    gboolean dispose_has_run;
};

static void destroyable_iface_init (gpointer g_iface, gpointer iface_data);
static void chat_state_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE(HazeIMChannel, haze_im_channel, TP_TYPE_BASE_CHANNEL,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_TEXT,
        tp_message_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_DESTROYABLE1,
        destroyable_iface_init);

    /* For some reason we reimplement ChatState rather than having the
     * TpMessageMixin do it :-( */
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_CHAT_STATE1,
        chat_state_iface_init))

static void
haze_im_channel_close (TpBaseChannel *base)
{
    HazeIMChannel *self = HAZE_IM_CHANNEL (base);

    /* The IM factory will resurrect the channel if we have pending
     * messages. When we're resurrected, we want the initiator
     * to be the contact who sent us those messages, if it isn't already */
    if (tp_message_mixin_has_pending_messages ((GObject *) self, NULL))
    {
        DEBUG ("Not really closing, I still have pending messages");
        tp_message_mixin_set_rescued ((GObject *) self);
        tp_base_channel_reopened (base,
            tp_base_channel_get_target_handle (base));
    }
    else
    {
        tp_clear_pointer (&self->priv->conv, purple_conversation_destroy);
        tp_base_channel_destroyed (base);
    }
}

static PurpleAccount *
haze_im_channel_get_account (HazeIMChannel *self)
{
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  HazeConnection *conn = HAZE_CONNECTION (base_conn);

  return conn->account;
}

static gboolean
_chat_state_available (HazeIMChannel *chan)
{
    PurplePluginProtocolInfo *prpl_info =
        PURPLE_PLUGIN_PROTOCOL_INFO (
            haze_im_channel_get_account (chan)->gc->prpl);

    return (prpl_info->send_typing != NULL);
}

static GPtrArray *
haze_im_channel_get_interfaces (TpBaseChannel *base)
{
  HazeIMChannel *self = HAZE_IM_CHANNEL (base);
  GPtrArray *interfaces;

  interfaces = TP_BASE_CHANNEL_CLASS (
      haze_im_channel_parent_class)->get_interfaces (base);

  if (_chat_state_available (self))
    g_ptr_array_add (interfaces, TP_IFACE_CHANNEL_INTERFACE_CHAT_STATE1);

  g_ptr_array_add (interfaces, TP_IFACE_CHANNEL_INTERFACE_DESTROYABLE1);

  return interfaces;
}

/**
 * haze_im_channel_destroy
 *
 * Implements D-Bus method Destroy
 * on interface im.telepathy.v1.Channel.Interface.Destroyable
 */
static void
haze_im_channel_destroy (TpSvcChannelInterfaceDestroyable1 *iface,
                         DBusGMethodInvocation *context)
{
    HazeIMChannel *self = HAZE_IM_CHANNEL (iface);

    g_assert (HAZE_IS_IM_CHANNEL (self));

    DEBUG ("called on %p", self);

    /* Clear out any pending messages */
    tp_message_mixin_clear ((GObject *) self);

    haze_im_channel_close (TP_BASE_CHANNEL (self));
    tp_svc_channel_interface_destroyable1_return_from_destroy (context);
}

static void
destroyable_iface_init (gpointer g_iface,
                        gpointer iface_data)
{
    TpSvcChannelInterfaceDestroyable1Class *klass = g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_destroyable1_implement_##x (\
    klass, haze_im_channel_##x)
    IMPLEMENT(destroy);
#undef IMPLEMENT
}

const gchar *typing_state_names[] = {
    "not typing",
    "typing",
    "typed"
};

static gboolean
resend_typing_cb (gpointer data)
{
    PurpleConversation *conv = (PurpleConversation *)data;
    HazeConversationUiData *ui_data = PURPLE_CONV_GET_HAZE_UI_DATA (conv);
    PurpleConnection *gc = purple_conversation_get_gc (conv);
    const gchar *who = purple_conversation_get_name (conv);
    PurpleTypingState typing = ui_data->active_state;

    DEBUG ("resending '%s' to %s", typing_state_names[typing], who);
    if (serv_send_typing (gc, who, typing))
    {
        return TRUE; /* Let's keep doing this thang. */
    }
    else
    {
        DEBUG ("clearing resend_typing_cb timeout");
        ui_data->resend_typing_timeout_id = 0;
        return FALSE;
    }
}


static void
haze_im_channel_set_chat_state (TpSvcChannelInterfaceChatState1 *self,
                                guint state,
                                DBusGMethodInvocation *context)
{
    HazeIMChannel *chan = HAZE_IM_CHANNEL (self);

    PurpleConversation *conv = chan->priv->conv;
    HazeConversationUiData *ui_data = PURPLE_CONV_GET_HAZE_UI_DATA (conv);
    PurpleConnection *gc = purple_conversation_get_gc (conv);
    const gchar *who = purple_conversation_get_name (conv);

    GError *error = NULL;
    PurpleTypingState typing = PURPLE_NOT_TYPING;
    guint timeout;

    g_assert (_chat_state_available (chan));

    if (ui_data->resend_typing_timeout_id)
    {
        DEBUG ("clearing existing resend_typing_cb timeout");
        g_source_remove (ui_data->resend_typing_timeout_id);
        ui_data->resend_typing_timeout_id = 0;
    }

    switch (state)
    {
        case TP_CHANNEL_CHAT_STATE_GONE:
            DEBUG ("The Gone state may not be explicitly set");
            g_set_error (&error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                "The Gone state may not be explicitly set");
            break;
        case TP_CHANNEL_CHAT_STATE_INACTIVE:
        case TP_CHANNEL_CHAT_STATE_ACTIVE:
            typing = PURPLE_NOT_TYPING;
            break;
        case TP_CHANNEL_CHAT_STATE_PAUSED:
            typing = PURPLE_TYPED;
            break;
        case TP_CHANNEL_CHAT_STATE_COMPOSING:
            typing = PURPLE_TYPING;
            break;
        default:
            DEBUG ("Invalid chat state: %u", state);
            g_set_error (&error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
                "Invalid chat state: %u", state);
    }

    if (error)
    {
          dbus_g_method_return_error (context, error);
          g_error_free (error);
          return;
    }

    DEBUG ("sending '%s' to %s", typing_state_names[typing], who);

    ui_data->active_state = typing;
    timeout = serv_send_typing (gc, who, typing);
    /* Apparently some protocols need you to repeatedly set the typing state,
     * so let's rig up a callback to do that.  serv_send_typing returns the
     * number of seconds till the state times out, or 0 if states don't time
     * out.
     *
     * That said, it would be stupid to repeatedly send not typing, so let's
     * not do that.
     */
    if (timeout && typing != PURPLE_NOT_TYPING)
    {
        ui_data->resend_typing_timeout_id = g_timeout_add (timeout * 1000,
            resend_typing_cb, conv);
    }

    tp_svc_channel_interface_chat_state1_return_from_set_chat_state (context);
}

static void
chat_state_iface_init (gpointer g_iface, gpointer iface_data)
{
    TpSvcChannelInterfaceChatState1Class *klass =
        (TpSvcChannelInterfaceChatState1Class *)g_iface;
#define IMPLEMENT(x) tp_svc_channel_interface_chat_state1_implement_##x (\
    klass, haze_im_channel_##x)
    IMPLEMENT(set_chat_state);
#undef IMPLEMENT
}

static void
haze_im_channel_send (GObject *obj,
                      TpMessage *message,
                      TpMessageSendingFlags send_flags)
{
  HazeIMChannel *self = HAZE_IM_CHANNEL (obj);
  GVariant *header = NULL, *body = NULL;
  const gchar *content_type, *text;
  guint type = 0;
  PurpleMessageFlags flags = 0;
  gchar *escaped, *line_broken, *reapostrophised;
  GError *error = NULL;

  if (tp_message_count_parts (message) != 2)
    {
      error = g_error_new (TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "messages must have a single plain-text part");
      goto err;
    }

  header = tp_message_dup_part (message, 0);
  body = tp_message_dup_part (message, 1);

  type = tp_vardict_get_uint32 (header, "message-type", NULL);
  g_variant_unref (header);

  content_type = tp_vardict_get_string (body, "content-type");
  text = tp_vardict_get_string (body, "content");

  if (tp_strdiff (content_type, "text/plain"))
    {
      error = g_error_new (TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "messages must have a single plain-text part");
      goto err;
    }

  if (text == NULL)
    {
      error = g_error_new (TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "message body must be a UTF-8 string");
      goto err;
    }

  switch (type)
    {
    case TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION:
      /* XXX this is not good enough for prpl-irc, which has a slash-command
       *     for actions and doesn't do special stuff to messages which happen
       *     to start with "/me ".
       */
      text = g_strconcat ("/me ", text, NULL);
      break;
    case TP_CHANNEL_TEXT_MESSAGE_TYPE_AUTO_REPLY:
      flags |= PURPLE_MESSAGE_AUTO_RESP;
      /* deliberate fall-through: */
    case TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL:
      text = g_strdup (text);
      break;
    /* TODO: libpurple should probably have a NOTICE flag, and then we could
     * support TP_CHANNEL_TEXT_MESSAGE_TYPE_NOTICE.
     */
    default:
      error = g_error_new (TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
          "unsupported message type: %u", type);
      goto err;
    }

  escaped = g_markup_escape_text (text, -1);
  /* avoid line breaks being swallowed! */
  line_broken = purple_strreplace (escaped, "\n", "<br>");
  /* This is a workaround for prpl-yahoo, which in libpurple <= 2.3.1 could
   * not deal with &apos; and would send it literally.
   * TODO: When we depend on new enough libpurple, remove this workaround.
   */
  reapostrophised = purple_strreplace (line_broken, "&apos;", "'");

  purple_conv_im_send_with_flags (PURPLE_CONV_IM (self->priv->conv),
      reapostrophised, flags);

  g_free (reapostrophised);
  g_free (line_broken);
  g_free (escaped);
  g_variant_unref (body);

  tp_message_mixin_sent (obj, message, 0, "", NULL);
  return;

err:
  g_assert (error != NULL);
  tp_message_mixin_sent (obj, message, 0, NULL, error);
  g_error_free (error);
  if (body != NULL)
    g_variant_unref (body);
}

static void
haze_im_channel_fill_immutable_properties (TpBaseChannel *chan,
    GHashTable *properties)
{
  TpBaseChannelClass *cls = TP_BASE_CHANNEL_CLASS (
      haze_im_channel_parent_class);

  cls->fill_immutable_properties (chan, properties);

  tp_dbus_properties_mixin_fill_properties_hash (
      G_OBJECT (chan), properties,
      TP_IFACE_CHANNEL_TYPE_TEXT, "MessagePartSupportFlags",
      TP_IFACE_CHANNEL_TYPE_TEXT, "DeliveryReportingSupport",
      TP_IFACE_CHANNEL_TYPE_TEXT, "SupportedContentTypes",
      TP_IFACE_CHANNEL_TYPE_TEXT, "MessageTypes",
      NULL);
}

static const TpChannelTextMessageType supported_message_types[] = {
    TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL,
    TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION,
    TP_CHANNEL_TEXT_MESSAGE_TYPE_AUTO_REPLY,
};

static const gchar * const supported_content_types[] = {
    "text/plain",
    NULL
};

static GObject *
haze_im_channel_constructor (GType type, guint n_props,
                             GObjectConstructParam *props)
{
    GObject *obj;
    HazeIMChannel *chan;
    HazeIMChannelPrivate *priv;
    TpBaseChannel *base;
    TpBaseConnection *conn;

    obj = G_OBJECT_CLASS (haze_im_channel_parent_class)->
        constructor (type, n_props, props);
    chan = HAZE_IM_CHANNEL (obj);
    base = TP_BASE_CHANNEL (obj);
    priv = chan->priv;
    conn = tp_base_channel_get_connection (base);

    tp_message_mixin_init (obj, G_STRUCT_OFFSET (HazeIMChannel, messages),
        conn);
    tp_message_mixin_implement_sending (obj, haze_im_channel_send, 3,
        supported_message_types, 0, 0, supported_content_types);

    priv->dispose_has_run = FALSE;

    return obj;
}

static void
haze_im_channel_dispose (GObject *obj)
{
    HazeIMChannel *chan = HAZE_IM_CHANNEL (obj);
    HazeIMChannelPrivate *priv = chan->priv;

    if (priv->dispose_has_run)
        return;
    priv->dispose_has_run = TRUE;

    tp_clear_pointer (&priv->conv, purple_conversation_destroy);

    tp_message_mixin_finalize (obj);

    G_OBJECT_CLASS (haze_im_channel_parent_class)->dispose (obj);
}

static gchar *
haze_im_channel_get_object_path_suffix (TpBaseChannel *chan)
{
  return g_strdup_printf ("IMChannel%u",
      tp_base_channel_get_target_handle (chan));
}

static void
haze_im_channel_class_init (HazeIMChannelClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    TpBaseChannelClass *base_class = TP_BASE_CHANNEL_CLASS (klass);

    g_type_class_add_private (klass, sizeof (HazeIMChannelPrivate));

    object_class->constructor = haze_im_channel_constructor;
    object_class->dispose = haze_im_channel_dispose;

    base_class->channel_type = TP_IFACE_CHANNEL_TYPE_TEXT;
    base_class->get_interfaces = haze_im_channel_get_interfaces;
    base_class->target_entity_type = TP_ENTITY_TYPE_CONTACT;
    base_class->close = haze_im_channel_close;
    base_class->fill_immutable_properties =
        haze_im_channel_fill_immutable_properties;
    base_class->get_object_path_suffix =
        haze_im_channel_get_object_path_suffix;

    tp_message_mixin_init_dbus_properties (object_class);
}

static void
haze_im_channel_init (HazeIMChannel *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, HAZE_TYPE_IM_CHANNEL,
                                              HazeIMChannelPrivate);
}

void
haze_im_channel_start (HazeIMChannel *self)
{
    const char *recipient;
    HazeIMChannelPrivate *priv = self->priv;
    TpHandleRepoIface *contact_handles;
    TpBaseChannel *base = TP_BASE_CHANNEL (self);
    TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
    HazeConnection *conn = HAZE_CONNECTION (base_conn);

    contact_handles = tp_base_connection_get_handles (base_conn,
        TP_ENTITY_TYPE_CONTACT);
    recipient = tp_handle_inspect (contact_handles,
        tp_base_channel_get_target_handle (base));
    priv->conv = purple_conversation_new (PURPLE_CONV_TYPE_IM,
        conn->account, recipient);
}

static TpMessage *
_make_message (HazeIMChannel *self,
               char *text_plain,
               PurpleMessageFlags flags,
               time_t mtime)
{
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  TpMessage *message = tp_cm_message_new (base_conn, 2);
  TpChannelTextMessageType type = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
  time_t now = time (NULL);

  if (flags & PURPLE_MESSAGE_AUTO_RESP)
    type = TP_CHANNEL_TEXT_MESSAGE_TYPE_AUTO_REPLY;
  else if (purple_message_meify (text_plain, -1))
    type = TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION;

  tp_cm_message_set_sender (message,
      tp_base_channel_get_target_handle (base));
  tp_message_set_uint32 (message, 0, "message-type", type);

  /* FIXME: the second half of this test shouldn't be necessary but prpl-jabber
   *        or the test are broken.
   */
  if (flags & PURPLE_MESSAGE_DELAYED || mtime != now)
    tp_message_set_int64 (message, 0, "message-sent", mtime);

  tp_message_set_int64 (message, 0, "message-received", now);

  /* Body */
  tp_message_set_string (message, 1, "content-type", "text/plain");
  tp_message_set_string (message, 1, "content", text_plain);

  return message;
}

static TpMessage *
_make_delivery_report (HazeIMChannel *self,
                       char *text_plain)
{
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  TpBaseConnection *base_conn = tp_base_channel_get_connection (base);
  TpMessage *report = tp_cm_message_new (base_conn, 2);

  /* "MUST be the intended recipient of the original message" */
  tp_cm_message_set_sender (report,
      tp_base_channel_get_target_handle (base));
  tp_message_set_uint32 (report, 0, "message-type",
      TP_CHANNEL_TEXT_MESSAGE_TYPE_DELIVERY_REPORT);
  /* FIXME: we don't know that the failure is temporary */
  tp_message_set_uint32 (report, 0, "delivery-status",
      TP_DELIVERY_STATUS_TEMPORARILY_FAILED);

  /* Put libpurple's localized human-readable error message both into the debug
   * info field in the header, and as the delivery report's body.
   */
  tp_message_set_string (report, 0, "delivery-error-message", text_plain);
  tp_message_set_string (report, 1, "content-type", "text/plain");
  tp_message_set_string (report, 1, "content", text_plain);

  return report;
}

void
haze_im_channel_receive (HazeIMChannel *self,
                         const char *xhtml_message,
                         PurpleMessageFlags flags,
                         time_t mtime)
{
  TpBaseChannel *base = TP_BASE_CHANNEL (self);
  gchar *line_broken, *text_plain;

  /* Replaces newline characters with <br>, which then get turned back into
   * newlines by purple_markup_strip_html (which replaces "\n" with " ")...
   */
  line_broken = purple_strdup_withhtml (xhtml_message);
  text_plain = purple_markup_strip_html (line_broken);
  g_free (line_broken);

  if (flags & PURPLE_MESSAGE_RECV)
    tp_message_mixin_take_received ((GObject *) self,
        _make_message (self, text_plain, flags, mtime));
  else if (flags & PURPLE_MESSAGE_SEND)
    {
      /* Do nothing: the message mixin emitted sent for us. */
    }
  else if (flags & PURPLE_MESSAGE_ERROR)
    tp_message_mixin_take_received ((GObject *) self,
        _make_delivery_report (self, text_plain));
  else
    DEBUG ("channel %u: ignoring message %s with flags %u",
        tp_base_channel_get_target_handle (base), text_plain, flags);

  g_free (text_plain);
}
