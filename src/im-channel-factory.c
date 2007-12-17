/*
 * im-channel-factory.c - HazeImChannelFactory source
 * Copyright (C) 2007 Will Thompson
 * Copyright (C) 2007 Collabora Ltd.
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

#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/handle-repo.h>
#include <telepathy-glib/base-connection.h>

#include "debug.h"
#include "im-channel.h"
#include "im-channel-factory.h"
#include "connection.h"

typedef struct _HazeImChannelFactoryPrivate HazeImChannelFactoryPrivate;
struct _HazeImChannelFactoryPrivate {
    HazeConnection *conn;
    GHashTable *channels;
    gboolean dispose_has_run;
};
#define HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE(o) \
    (G_TYPE_INSTANCE_GET_PRIVATE((o), HAZE_TYPE_IM_CHANNEL_FACTORY, \
                                 HazeImChannelFactoryPrivate))

static void haze_im_channel_factory_iface_init (gpointer g_iface,
                                                gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE(HazeImChannelFactory,
    haze_im_channel_factory,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE,
      haze_im_channel_factory_iface_init));

/* properties: */
enum {
    PROP_CONNECTION = 1,
    
    LAST_PROPERTY
};

static HazeIMChannel *
get_im_channel (HazeImChannelFactory *self,
                TpHandle handle,
                gboolean *created);

static void
conversation_updated_cb (PurpleConversation *conv,
                         PurpleConvUpdateType type,
                         gpointer unused)
{
    PurpleAccount *account = purple_conversation_get_account (conv);
    HazeImChannelFactory *im_factory =
        ACCOUNT_GET_HAZE_CONNECTION (account)->im_factory;
    HazeConversationUiData *ui_data;
    HazeIMChannel *chan;

    PurpleTypingState typing;
    TpChannelChatState state;

    if (type != PURPLE_CONV_UPDATE_TYPING)
        return;

    if (conv->type != PURPLE_CONV_TYPE_IM)
    {
        DEBUG ("typing state update for a non-IM chat, ignoring");
        return;
    }

    ui_data = PURPLE_CONV_GET_HAZE_UI_DATA (conv);
    typing = purple_conv_im_get_typing_state (PURPLE_CONV_IM (conv));

    switch (typing)
    {
        case PURPLE_TYPING:
            state = TP_CHANNEL_CHAT_STATE_COMPOSING;
            break;
        case PURPLE_TYPED:
            state = TP_CHANNEL_CHAT_STATE_PAUSED;
            break;
        case PURPLE_NOT_TYPING:
            state = TP_CHANNEL_CHAT_STATE_ACTIVE;
            break;
        default:
            g_assert_not_reached ();
    }

    chan = get_im_channel (im_factory, ui_data->contact_handle, NULL);

    tp_svc_channel_interface_chat_state_emit_chat_state_changed (
        (TpSvcChannelInterfaceChatState*)chan, ui_data->contact_handle, state);
}

static void
haze_im_channel_factory_init (HazeImChannelFactory *self)
{
    HazeImChannelFactoryPrivate *priv =
        HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE(self);

    priv->channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                            NULL, g_object_unref);

    priv->conn = NULL;
    priv->dispose_has_run = FALSE;
}

static void
haze_im_channel_factory_dispose (GObject *object)
{
    HazeImChannelFactory *self = HAZE_IM_CHANNEL_FACTORY (object);
    HazeImChannelFactoryPrivate *priv =
        HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE(self);

    if (priv->dispose_has_run)
        return;

    priv->dispose_has_run = TRUE;

    tp_channel_factory_iface_close_all (TP_CHANNEL_FACTORY_IFACE (object));
    g_assert (priv->channels == NULL);

    if (G_OBJECT_CLASS (haze_im_channel_factory_parent_class)->dispose)
        G_OBJECT_CLASS (haze_im_channel_factory_parent_class)->dispose (object);
}

static void
haze_im_channel_factory_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    HazeImChannelFactory *fac = HAZE_IM_CHANNEL_FACTORY (object);
    HazeImChannelFactoryPrivate *priv =
        HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE (fac);

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
haze_im_channel_factory_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    HazeImChannelFactory *fac = HAZE_IM_CHANNEL_FACTORY (object);
    HazeImChannelFactoryPrivate *priv =
        HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE (fac);

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
haze_im_channel_factory_class_init (HazeImChannelFactoryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GParamSpec *param_spec;
    void *conv_handle = purple_conversations_get_handle();

    object_class->dispose = haze_im_channel_factory_dispose;
    object_class->get_property = haze_im_channel_factory_get_property;
    object_class->set_property = haze_im_channel_factory_set_property;

    param_spec = g_param_spec_object ("connection", "HazeConnection object",
                                      "Haze connection object that owns this "
                                      "IM channel factory object.",
                                      HAZE_TYPE_CONNECTION,
                                      G_PARAM_CONSTRUCT_ONLY |
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NICK |
                                      G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

    g_type_class_add_private (object_class,
                              sizeof(HazeImChannelFactoryPrivate));

    purple_signal_connect (conv_handle, "conversation-updated", klass,
        (PurpleCallback) conversation_updated_cb, NULL);
}

static void
im_channel_closed_cb (HazeIMChannel *chan, gpointer user_data)
{
    HazeImChannelFactory *fac = HAZE_IM_CHANNEL_FACTORY (user_data);
    HazeImChannelFactoryPrivate *priv =
        HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE (fac);
    TpHandle contact_handle;

    if (priv->channels)
    {
        g_object_get (chan, "handle", &contact_handle, NULL);

        DEBUG ("removing channel with handle %d", contact_handle);

        g_hash_table_remove (priv->channels, GINT_TO_POINTER (contact_handle));
    }
}

static HazeIMChannel *
new_im_channel (HazeImChannelFactory *self,
                guint handle)
{
    HazeImChannelFactoryPrivate *priv;
    TpBaseConnection *conn;
    HazeIMChannel *chan;
    char *object_path;

    g_assert (HAZE_IS_IM_CHANNEL_FACTORY (self));

    priv = HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE (self);
    conn = (TpBaseConnection *)priv->conn;

    g_assert (!g_hash_table_lookup (priv->channels, GINT_TO_POINTER (handle)));

    object_path = g_strdup_printf ("%s/ImChannel%u", conn->object_path, handle);

    chan = g_object_new (HAZE_TYPE_IM_CHANNEL,
                         "connection", priv->conn,
                         "object-path", object_path,
                         "handle", handle,
                         NULL);

    DEBUG ("Created IM channel with object path %s", object_path);

    g_signal_connect (chan, "closed", G_CALLBACK (im_channel_closed_cb), self);

    g_hash_table_insert (priv->channels, GINT_TO_POINTER (handle), chan);

    tp_channel_factory_iface_emit_new_channel (self, (TpChannelIface *)chan,
            NULL);

    g_free (object_path);

    return chan;
}

static HazeIMChannel *
get_im_channel (HazeImChannelFactory *self,
                TpHandle handle,
                gboolean *created)
{
    HazeImChannelFactoryPrivate *priv =
        HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE (self);
    HazeIMChannel *chan =
        g_hash_table_lookup (priv->channels, GINT_TO_POINTER (handle));

    if (chan)
    {
        if (created)
            *created = FALSE;
    }
    else
    {
        chan = new_im_channel (self, handle);
        if (created)
            *created = TRUE;
    }
    g_assert (chan);
    return chan;
}

static void
haze_im_channel_factory_iface_close_all (TpChannelFactoryIface *iface)
{
    HazeImChannelFactory *fac = HAZE_IM_CHANNEL_FACTORY (iface);
    HazeImChannelFactoryPrivate *priv =
        HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE (fac);
    GHashTable *tmp;

    DEBUG ("closing im channels");

    if (priv->channels)
    {
        tmp = priv->channels;
        priv->channels = NULL;
        g_hash_table_destroy (tmp);
    }
}

static void
haze_im_channel_factory_iface_connecting (TpChannelFactoryIface *iface)
{
    /* HazeImChannelFactory *self = HAZE_IM_CHANNEL_FACTORY (iface); */
}

static void
haze_im_channel_factory_iface_disconnected (TpChannelFactoryIface *iface)
{
    /* HazeImChannelFactory *self = HAZE_IM_CHANNEL_FACTORY (iface); */
}

struct _ForeachData
{
    TpChannelFunc foreach;
    gpointer user_data;
};

static void
_foreach_slave (gpointer key, gpointer value, gpointer user_data)
{
    struct _ForeachData *data = (struct _ForeachData *) user_data;
    TpChannelIface *chan = TP_CHANNEL_IFACE (value);

    data->foreach (chan, data->user_data);
}

static void
haze_im_channel_factory_iface_foreach (TpChannelFactoryIface *iface,
                                       TpChannelFunc foreach,
                                       gpointer user_data)
{
    HazeImChannelFactory *fac = HAZE_IM_CHANNEL_FACTORY (iface);
    HazeImChannelFactoryPrivate *priv =
        HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE (fac);
    struct _ForeachData data;

    data.user_data = user_data;
    data.foreach = foreach;

    g_hash_table_foreach (priv->channels, _foreach_slave, &data);
}

static TpChannelFactoryRequestStatus
haze_im_channel_factory_iface_request (TpChannelFactoryIface *iface,
                                       const gchar *chan_type,
                                       TpHandleType handle_type,
                                       guint handle,
                                       gpointer request,
                                       TpChannelIface **ret,
                                       GError **error)
{
    HazeImChannelFactory *self = HAZE_IM_CHANNEL_FACTORY (iface);
    HazeImChannelFactoryPrivate *priv =
        HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE (self);
    TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
            (TpBaseConnection *)priv->conn, TP_HANDLE_TYPE_CONTACT);
    HazeIMChannel *chan;
    TpChannelFactoryRequestStatus status;
    gboolean created;

    if (strcmp (chan_type, TP_IFACE_CHANNEL_TYPE_TEXT))
        return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;

    if (handle_type != TP_HANDLE_TYPE_CONTACT)
        return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;

    if (!tp_handle_is_valid (contact_repo, handle, error))
        return TP_CHANNEL_FACTORY_REQUEST_STATUS_ERROR;

    chan = get_im_channel (self, handle, &created);
    if (created)
    {
        status = TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED;
    }
    else
    {
        status = TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING;
    }

    *ret = TP_CHANNEL_IFACE (chan);
    return status;
}

static void
haze_im_channel_factory_iface_init (gpointer g_iface,
                                    gpointer iface_data)
{
    TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *) g_iface;

    klass->close_all = haze_im_channel_factory_iface_close_all;
    klass->connecting = haze_im_channel_factory_iface_connecting;
    klass->connected = NULL; //haze_im_channel_factory_iface_connected;
    klass->disconnected = haze_im_channel_factory_iface_disconnected;
    klass->foreach = haze_im_channel_factory_iface_foreach;
    klass->request = haze_im_channel_factory_iface_request;
}

static void
haze_write_im (PurpleConversation *conv,
               const char *who,
               const char *xhtml_message,
               PurpleMessageFlags flags,
               time_t mtime)
{
    PurpleAccount *account = purple_conversation_get_account (conv);

    HazeImChannelFactory *im_factory =
        ACCOUNT_GET_HAZE_CONNECTION (account)->im_factory;
    TpChannelTextMessageType type = TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL;
    HazeIMChannel *chan = NULL;
    char *line_broken, *message;

    HazeConversationUiData *ui_data = PURPLE_CONV_GET_HAZE_UI_DATA (conv);

    /* Replaces newline characters with <br>, which then get turned back into
     * newlines by purple_markup_strip_html (which replaces "\n" with " ")...
     */
    line_broken = purple_strdup_withhtml (xhtml_message);
    message = purple_markup_strip_html (line_broken);
    g_free (line_broken);

    if (flags & PURPLE_MESSAGE_AUTO_RESP)
        type = TP_CHANNEL_TEXT_MESSAGE_TYPE_AUTO_REPLY;
    else if (purple_message_meify(message, -1))
        type = TP_CHANNEL_TEXT_MESSAGE_TYPE_ACTION;

    chan = get_im_channel (im_factory, ui_data->contact_handle, NULL);

    if (flags & PURPLE_MESSAGE_RECV)
        tp_text_mixin_receive (G_OBJECT (chan), type, ui_data->contact_handle,
                               mtime, message);
    else if (flags & PURPLE_MESSAGE_SEND)
        tp_svc_channel_type_text_emit_sent (chan, mtime, type, message);
    else if (flags & PURPLE_MESSAGE_ERROR)
        /* This is wrong.  The mtime, type and message are of the error message
         * (such as "Unable to send message: The message is too large.") not of
         * the message causing the error, and the ChannelTextSendError parameter
         * shouldn't always be unknown.  But this is the best that can be done
         * until I fix libpurple.
         */
        tp_svc_channel_type_text_emit_send_error (chan,
            TP_CHANNEL_TEXT_SEND_ERROR_UNKNOWN, mtime, type, message);
    else
        DEBUG ("channel %u: ignoring message %s with flags %u",
            ui_data->contact_handle, message, flags);

    g_free (message);
}

static void
haze_write_conv (PurpleConversation *conv,
                 const char *name,
                 const char *alias,
                 const char *message,
                 PurpleMessageFlags flags,
                 time_t mtime)
{
    PurpleConversationType type = purple_conversation_get_type (conv);
    switch (type)
    {
        case PURPLE_CONV_TYPE_IM:
            haze_write_im (conv, name, message, flags, mtime);
            break;
        default:
            DEBUG ("ignoring message to conv type %u (flags=%u; message=%s)",
                type, flags, message);
    }
}

static void
haze_create_conversation (PurpleConversation *conv)
{
    PurpleAccount *account = purple_conversation_get_account (conv);

    HazeImChannelFactory *im_factory =
        ACCOUNT_GET_HAZE_CONNECTION (account)->im_factory;
    HazeImChannelFactoryPrivate *priv =
        HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE (im_factory);
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->conn);
    TpHandleRepoIface *contact_repo =
        tp_base_connection_get_handles (base_conn, TP_HANDLE_TYPE_CONTACT);

    const gchar *who = purple_conversation_get_name (conv);

    HazeConversationUiData *ui_data;

    DEBUG ("(PurpleConversation *)%p created", conv);

    if (conv->type != PURPLE_CONV_TYPE_IM)
    {
        DEBUG ("not an IM conversation; ignoring");
        return;
    }

    g_assert (who);

    conv->ui_data = ui_data = g_slice_new0 (HazeConversationUiData);

    ui_data->contact_handle = tp_handle_ensure (contact_repo, who, NULL, NULL);
    g_assert (ui_data->contact_handle);
}

static void
haze_destroy_conversation (PurpleConversation *conv)
{
    PurpleAccount *account = purple_conversation_get_account (conv);

    HazeImChannelFactory *im_factory =
        ACCOUNT_GET_HAZE_CONNECTION (account)->im_factory;
    HazeImChannelFactoryPrivate *priv =
        HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE (im_factory);
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->conn);
    TpHandleRepoIface *contact_repo =
        tp_base_connection_get_handles (base_conn, TP_HANDLE_TYPE_CONTACT);

    HazeConversationUiData *ui_data;

    DEBUG ("(PurpleConversation *)%p destroyed", conv);
    if (conv->type != PURPLE_CONV_TYPE_IM)
    {
        DEBUG ("not an IM conversation; ignoring");
        return;
    }

    ui_data = PURPLE_CONV_GET_HAZE_UI_DATA (conv);

    tp_handle_unref (contact_repo, ui_data->contact_handle);
    if (ui_data->resend_typing_timeout_id)
        g_source_remove (ui_data->resend_typing_timeout_id);

    g_slice_free (HazeConversationUiData, ui_data);
    conv->ui_data = NULL;
}

static PurpleConversationUiOps
conversation_ui_ops =
{
    haze_create_conversation,  /* create_conversation */
    haze_destroy_conversation, /* destroy_conversation */
    NULL,                      /* write_chat */
    haze_write_im,             /* write_im */
    haze_write_conv,           /* write_conv */
    NULL,                      /* chat_add_users */
    NULL,                      /* chat_rename_user */
    NULL,                      /* chat_remove_users */
    NULL,                      /* chat_update_user */

    NULL,                      /* present */

    NULL,                      /* has_focus */

    NULL,                      /* custom_smiley_add */
    NULL,                      /* custom_smiley_write */
    NULL,                      /* custom_smiley_close */

    NULL,                      /* send_confirm */

    NULL,                      /* _purple_reserved1 */
    NULL,                      /* _purple_reserved2 */
    NULL,                      /* _purple_reserved3 */
    NULL,                      /* _purple_reserved4 */
};

PurpleConversationUiOps *
haze_get_conv_ui_ops(void)
{
    return &conversation_ui_ops;
}
