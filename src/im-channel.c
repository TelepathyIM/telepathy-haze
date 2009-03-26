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

#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/exportable-channel.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-generic.h>

#include "im-channel.h"
#include "connection.h"
#include "debug.h"

/* properties */
enum
{
  PROP_CONNECTION = 1,
  PROP_OBJECT_PATH,
  PROP_CHANNEL_TYPE,
  PROP_HANDLE_TYPE,
  PROP_HANDLE,
  PROP_TARGET_ID,
  PROP_INTERFACES,
  PROP_INITIATOR_HANDLE,
  PROP_INITIATOR_ID,
  PROP_REQUESTED,
  PROP_CHANNEL_PROPERTIES,
  PROP_CHANNEL_DESTROYED,

  LAST_PROPERTY
};

struct _HazeIMChannelPrivate
{
    HazeConnection *conn;
    char *object_path;
    TpHandle handle;
    TpHandle initiator;

    PurpleConversation *conv;

    gboolean closed;
    gboolean dispose_has_run;
};

static void channel_iface_init (gpointer, gpointer);
static void destroyable_iface_init (gpointer g_iface, gpointer iface_data);
static void chat_state_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE(HazeIMChannel, haze_im_channel, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_TEXT,
        tp_message_mixin_text_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_MESSAGES,
        tp_message_mixin_messages_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_DESTROYABLE,
        destroyable_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_CHAT_STATE,
        chat_state_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
        tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL))

static void
haze_im_channel_close (TpSvcChannel *iface,
                       DBusGMethodInvocation *context)
{
    HazeIMChannel *self = HAZE_IM_CHANNEL (iface);
    HazeIMChannelPrivate *priv = self->priv;

    if (priv->closed)
    {
        DEBUG ("Already closed");
        goto out;
    }

    /* requires support from TpChannelManager */
    if (tp_message_mixin_has_pending_messages ((GObject *) self, NULL))
    {
        if (priv->initiator != priv->handle)
        {
            TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
                (TpBaseConnection *) priv->conn, TP_HANDLE_TYPE_CONTACT);

            g_assert (priv->initiator != 0);
            g_assert (priv->handle != 0);

            tp_handle_unref (contact_repo, priv->initiator);
            priv->initiator = priv->handle;
            tp_handle_ref (contact_repo, priv->initiator);
        }

        tp_message_mixin_set_rescued ((GObject *) self);
    }
    else
    {
        purple_conversation_destroy (priv->conv);
        priv->conv = NULL;
        priv->closed = TRUE;
    }

    tp_svc_channel_emit_closed (iface);

out:
    tp_svc_channel_return_from_close(context);
}

static void
haze_im_channel_get_channel_type (TpSvcChannel *iface,
                                  DBusGMethodInvocation *context)
{
    tp_svc_channel_return_from_get_channel_type (context,
        TP_IFACE_CHANNEL_TYPE_TEXT);
}

static void
haze_im_channel_get_handle (TpSvcChannel *iface,
                            DBusGMethodInvocation *context)
{
    HazeIMChannel *self = HAZE_IM_CHANNEL (iface);

    tp_svc_channel_return_from_get_handle (context, TP_HANDLE_TYPE_CONTACT,
        self->priv->handle);
}

static gboolean
_chat_state_available (HazeIMChannel *chan)
{
    PurplePluginProtocolInfo *prpl_info =
        PURPLE_PLUGIN_PROTOCOL_INFO (chan->priv->conn->account->gc->prpl);

    return (prpl_info->send_typing != NULL);
}

static const char * const*
_haze_im_channel_interfaces (HazeIMChannel *chan)
{
  static const char * const interfaces[] = {
      TP_IFACE_CHANNEL_INTERFACE_CHAT_STATE,
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES,
      TP_IFACE_CHANNEL_INTERFACE_DESTROYABLE,
      NULL
  };

  if (_chat_state_available (chan))
    return interfaces;
  else
    return interfaces + 1;
}

static void
haze_im_channel_get_interfaces (TpSvcChannel *iface,
                                DBusGMethodInvocation *context)
{
    tp_svc_channel_return_from_get_interfaces (context,
        (const char **)_haze_im_channel_interfaces (HAZE_IM_CHANNEL (iface)));
}

static void
channel_iface_init (gpointer g_iface, gpointer iface_data)
{
    TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, haze_im_channel_##x)
    IMPLEMENT(close);
    IMPLEMENT(get_channel_type);
    IMPLEMENT(get_handle);
    IMPLEMENT(get_interfaces);
#undef IMPLEMENT
}

/**
 * haze_im_channel_destroy
 *
 * Implements D-Bus method Destroy
 * on interface org.freedesktop.Telepathy.Channel.Interface.Destroyable
 */
static void
haze_im_channel_destroy (TpSvcChannelInterfaceDestroyable *iface,
                         DBusGMethodInvocation *context)
{
    HazeIMChannel *self = HAZE_IM_CHANNEL (iface);

    g_assert (HAZE_IS_IM_CHANNEL (self));

    DEBUG ("called on %p", self);

    /* Clear out any pending messages */
    tp_message_mixin_clear ((GObject *) self);

    /* Close() and Destroy() have the same signature, so we can safely
     * chain to the other function now */
    haze_im_channel_close ((TpSvcChannel *) self, context);
}

static void
destroyable_iface_init (gpointer g_iface,
                        gpointer iface_data)
{
    TpSvcChannelInterfaceDestroyableClass *klass = g_iface;

#define IMPLEMENT(x) tp_svc_channel_interface_destroyable_implement_##x (\
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
haze_im_channel_set_chat_state (TpSvcChannelInterfaceChatState *self,
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
            g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
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
            g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
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

    tp_svc_channel_interface_chat_state_return_from_set_chat_state (context);
}

static void
chat_state_iface_init (gpointer g_iface, gpointer iface_data)
{
    TpSvcChannelInterfaceChatStateClass *klass =
        (TpSvcChannelInterfaceChatStateClass *)g_iface;
#define IMPLEMENT(x) tp_svc_channel_interface_chat_state_implement_##x (\
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
  const GHashTable *header, *body;
  const gchar *content_type, *text;
  guint type = 0;
  PurpleMessageFlags flags = 0;
  gchar *escaped, *line_broken, *reapostrophised;
  GError *error = NULL;

  if (tp_message_count_parts (message) != 2)
    {
      error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "messages must have a single plain-text part");
      goto err;
    }

  header = tp_message_peek (message, 0);
  body = tp_message_peek (message, 1);

  type = tp_asv_get_uint32 (header, "message-type", NULL);
  content_type = tp_asv_get_string (body, "content-type");
  text = tp_asv_get_string (body, "content");

  if (tp_strdiff (content_type, "text/plain"))
    {
      error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "messages must have a single plain-text part");
      goto err;
    }

  if (text == NULL)
    {
      error = g_error_new (TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
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
      error = g_error_new (TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
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

  tp_message_mixin_sent (obj, message, 0, "", NULL);
  return;

err:
  g_assert (error != NULL);
  tp_message_mixin_sent (obj, message, 0, NULL, error);
  g_error_free (error);
}

static void
haze_im_channel_get_property (GObject    *object,
                              guint       property_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    HazeIMChannel *chan = HAZE_IM_CHANNEL (object);
    HazeIMChannelPrivate *priv = chan->priv;
    TpBaseConnection *base_conn = (TpBaseConnection *) priv->conn;

    switch (property_id) {
        case PROP_OBJECT_PATH:
            g_value_set_string (value, priv->object_path);
            break;
        case PROP_CHANNEL_TYPE:
            g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_TEXT);
            break;
        case PROP_HANDLE_TYPE:
            g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
            break;
        case PROP_HANDLE:
            g_value_set_uint (value, priv->handle);
            break;
        case PROP_TARGET_ID:
        {
            TpHandleRepoIface *repo = tp_base_connection_get_handles (base_conn,
                TP_HANDLE_TYPE_CONTACT);

            g_value_set_string (value, tp_handle_inspect (repo, priv->handle));
            break;
        }
        case PROP_INITIATOR_HANDLE:
            g_value_set_uint (value, priv->initiator);
            break;
        case PROP_INITIATOR_ID:
        {
            TpHandleRepoIface *repo = tp_base_connection_get_handles (base_conn,
                TP_HANDLE_TYPE_CONTACT);

            g_value_set_string (value, tp_handle_inspect (repo, priv->initiator));
            break;
        }
        case PROP_REQUESTED:
            g_value_set_boolean (value,
                (priv->initiator == base_conn->self_handle));
            break;
        case PROP_CONNECTION:
            g_value_set_object (value, priv->conn);
            break;
        case PROP_INTERFACES:
            g_value_set_boxed (value, _haze_im_channel_interfaces (chan));
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
                    NULL));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
haze_im_channel_set_property (GObject     *object,
                              guint        property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    HazeIMChannel *chan = HAZE_IM_CHANNEL (object);
    HazeIMChannelPrivate *priv = chan->priv;

    switch (property_id) {
        case PROP_OBJECT_PATH:
            g_free (priv->object_path);
            priv->object_path = g_value_dup_string (value);
            break;
        case PROP_HANDLE:
            /* we don't ref it here because we don't have access to the
             * contact repo yet - instead we ref it in the constructor.
             */
            priv->handle = g_value_get_uint (value);
            break;
        case PROP_INITIATOR_HANDLE:
            /* similarly we can't ref this yet */
            priv->initiator = g_value_get_uint (value);
            break;
        case PROP_CHANNEL_TYPE:
        case PROP_HANDLE_TYPE:
            /* this property is writable in the interface, but not actually
             * meaningfully changable on this channel, so we do nothing.
             */
            break;
        case PROP_CONNECTION:
            priv->conn = g_value_get_object (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
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
    TpHandleRepoIface *contact_handles;
    TpBaseConnection *conn;
    const char *recipient;
    DBusGConnection *bus;

    obj = G_OBJECT_CLASS (haze_im_channel_parent_class)->
        constructor (type, n_props, props);
    chan = HAZE_IM_CHANNEL (obj);
    priv = chan->priv;
    conn = (TpBaseConnection *) (priv->conn);

    contact_handles = tp_base_connection_get_handles (conn,
        TP_HANDLE_TYPE_CONTACT);
    tp_handle_ref (contact_handles, priv->handle);
    g_assert (priv->initiator != 0);
    tp_handle_ref (contact_handles, priv->initiator);

    tp_message_mixin_init (obj, G_STRUCT_OFFSET (HazeIMChannel, messages),
        conn);
    tp_message_mixin_implement_sending (obj, haze_im_channel_send, 3,
        supported_message_types, 0, 0, supported_content_types);

    bus = tp_get_bus ();
    dbus_g_connection_register_g_object (bus, priv->object_path, obj);

    recipient = tp_handle_inspect(contact_handles, priv->handle);
    priv->conv = purple_conversation_new (PURPLE_CONV_TYPE_IM,
                                          priv->conn->account,
                                          recipient);
    priv->closed = FALSE;
    priv->dispose_has_run = FALSE;

    return obj;
}

static void
haze_im_channel_dispose (GObject *obj)
{
    HazeIMChannel *chan = HAZE_IM_CHANNEL (obj);
    HazeIMChannelPrivate *priv = chan->priv;
    TpBaseConnection *conn = (TpBaseConnection *) priv->conn;
    TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (conn,
        TP_HANDLE_TYPE_CONTACT);

    if (priv->dispose_has_run)
        return;
    priv->dispose_has_run = TRUE;

    if (priv->handle != 0)
        tp_handle_unref (contact_handles, priv->handle);

    if (priv->initiator != 0)
        tp_handle_unref (contact_handles, priv->initiator);

    if (!priv->closed)
    {
        purple_conversation_destroy (priv->conv);
        priv->conv = NULL;
        tp_svc_channel_emit_closed (obj);
        priv->closed = TRUE;
    }

    g_free (priv->object_path);
    tp_message_mixin_finalize (obj);
}

static void
haze_im_channel_class_init (HazeIMChannelClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GParamSpec *param_spec;

    static gboolean properties_mixin_initialized = FALSE;
    static TpDBusPropertiesMixinPropImpl channel_props[] = {
        { "TargetHandleType", "handle-type", NULL },
        { "TargetHandle", "handle", NULL },
        { "TargetID", "target-id", NULL },
        { "ChannelType", "channel-type", NULL },
        { "Interfaces", "interfaces", NULL },
        { "Requested", "requested", NULL },
        { "InitiatorHandle", "initiator-handle", NULL },
        { "InitiatorID", "initiator-id", NULL },
        { NULL }
    };
    static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
        { TP_IFACE_CHANNEL,
          tp_dbus_properties_mixin_getter_gobject_properties,
          NULL,
          channel_props,
        },
        { NULL }
    };


    g_type_class_add_private (klass, sizeof (HazeIMChannelPrivate));

    object_class->get_property = haze_im_channel_get_property;
    object_class->set_property = haze_im_channel_set_property;
    object_class->constructor = haze_im_channel_constructor;
    object_class->dispose = haze_im_channel_dispose;

    g_object_class_override_property (object_class, PROP_OBJECT_PATH,
        "object-path");
    g_object_class_override_property (object_class, PROP_CHANNEL_TYPE,
        "channel-type");
    g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
        "handle-type");
    g_object_class_override_property (object_class, PROP_HANDLE,
        "handle");
    g_object_class_override_property (object_class, PROP_CHANNEL_DESTROYED,
        "channel-destroyed");
    g_object_class_override_property (object_class, PROP_CHANNEL_PROPERTIES,
        "channel-properties");

    param_spec = g_param_spec_object ("connection", "HazeConnection object",
        "Haze connection object that owns this IM channel object.",
        HAZE_TYPE_CONNECTION,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

    param_spec = g_param_spec_boxed ("interfaces", "Extra D-Bus interfaces",
        "Additional Channel.Interface.* interfaces",
        G_TYPE_STRV,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property (object_class, PROP_INTERFACES, param_spec);

    param_spec = g_param_spec_string ("target-id", "Other person's username",
        "The username of the other person in the conversation",
        NULL,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property (object_class, PROP_TARGET_ID, param_spec);

    param_spec = g_param_spec_boolean ("requested", "Requested?",
        "True if this channel was requested by the local user",
        FALSE,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property (object_class, PROP_REQUESTED, param_spec);

    param_spec = g_param_spec_uint ("initiator-handle", "Initiator's handle",
        "The contact who initiated the channel",
        0, G_MAXUINT32, 0,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property (object_class, PROP_INITIATOR_HANDLE,
        param_spec);

    param_spec = g_param_spec_string ("initiator-id", "Initiator's ID",
        "The string obtained by inspecting the initiator-handle",
        NULL,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property (object_class, PROP_INITIATOR_ID,
        param_spec);


    if (!properties_mixin_initialized)
    {
        properties_mixin_initialized = TRUE;
        klass->properties_class.interfaces = prop_interfaces;
        tp_dbus_properties_mixin_class_init (object_class,
            G_STRUCT_OFFSET (HazeIMChannelClass, properties_class));

        tp_message_mixin_init_dbus_properties (object_class);
    }
}

static void
haze_im_channel_init (HazeIMChannel *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, HAZE_TYPE_IM_CHANNEL,
                                              HazeIMChannelPrivate);
}

static TpMessage *
_make_message (HazeIMChannel *self,
               char *text_plain,
               PurpleMessageFlags flags,
               time_t mtime)
{
  TpMessage *message = tp_message_new ((TpBaseConnection *) self->priv->conn,
      2, 2);
  TpChannelTextMessageType type = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
  time_t now = time (NULL);

  if (flags & PURPLE_MESSAGE_AUTO_RESP)
    type = TP_CHANNEL_TEXT_MESSAGE_TYPE_AUTO_REPLY;
  else if (purple_message_meify (text_plain, -1))
    type = TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION;

  tp_message_set_handle (message, 0, "message-sender", TP_HANDLE_TYPE_CONTACT,
      self->priv->handle);
  tp_message_set_uint32 (message, 0, "message-type", type);

  /* FIXME: the second half of this test shouldn't be necessary but prpl-jabber
   *        or the test are broken.
   */
  if (flags & PURPLE_MESSAGE_DELAYED || mtime != now)
    tp_message_set_uint64 (message, 0, "message-sent", mtime);

  tp_message_set_uint64 (message, 0, "message-received", now);

  /* Body */
  tp_message_set_string (message, 1, "content-type", "text/plain");
  tp_message_set_string (message, 1, "content", text_plain);

  return message;
}

static TpMessage *
_make_delivery_report (HazeIMChannel *self,
                       char *text_plain)
{
  TpMessage *report = tp_message_new ((TpBaseConnection *) self->priv->conn, 2,
      2);

  /* "MUST be the intended recipient of the original message" */
  tp_message_set_uint32 (report, 0, "message-sender", self->priv->handle);
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
        self->priv->handle, text_plain, flags);

  g_free (text_plain);
}
