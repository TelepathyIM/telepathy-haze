/*
 * im-channel-factory.c - HazeImChannelFactory source
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

#include "config.h"
#include "im-channel-factory.h"

#include <string.h>

#include <telepathy-glib/base-connection.h>
#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/handle-repo.h>
#include <telepathy-glib/interfaces.h>

#include "debug.h"
#include "im-channel.h"
#include "connection.h"

struct _HazeImChannelFactoryPrivate {
    HazeConnection *conn;
    GHashTable *channels;
    gulong status_changed_id;
    gboolean dispose_has_run;
};

static void channel_manager_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(HazeImChannelFactory,
    haze_im_channel_factory,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      channel_manager_iface_init))

/* properties: */
enum {
    PROP_CONNECTION = 1,
    
    LAST_PROPERTY
};

static HazeIMChannel *get_im_channel (HazeImChannelFactory *self,
    TpHandle handle, TpHandle initiator, gpointer request_token,
    gboolean *created);
static void close_all (HazeImChannelFactory *self);

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

    chan = get_im_channel (im_factory, ui_data->contact_handle,
        ui_data->contact_handle, NULL, NULL);

    tp_svc_channel_interface_chat_state_emit_chat_state_changed (
        (TpSvcChannelInterfaceChatState*)chan, ui_data->contact_handle, state);
}

static void
haze_im_channel_factory_init (HazeImChannelFactory *self)
{
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
        HAZE_TYPE_IM_CHANNEL_FACTORY, HazeImChannelFactoryPrivate);

    self->priv->channels = g_hash_table_new_full (NULL, NULL,
        NULL, g_object_unref);
    self->priv->conn = NULL;
    self->priv->dispose_has_run = FALSE;
}

static void
haze_im_channel_factory_dispose (GObject *object)
{
    HazeImChannelFactory *self = HAZE_IM_CHANNEL_FACTORY (object);

    if (self->priv->dispose_has_run)
        return;

    self->priv->dispose_has_run = TRUE;

    close_all (self);
    g_assert (self->priv->channels == NULL);

    if (G_OBJECT_CLASS (haze_im_channel_factory_parent_class)->dispose)
        G_OBJECT_CLASS (haze_im_channel_factory_parent_class)->dispose (object);
}

static void
haze_im_channel_factory_get_property (GObject *object,
                                      guint property_id,
                                      GValue *value,
                                      GParamSpec *pspec)
{
    HazeImChannelFactory *self = HAZE_IM_CHANNEL_FACTORY (object);

    switch (property_id) {
        case PROP_CONNECTION:
            g_value_set_object (value, self->priv->conn);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
haze_im_channel_factory_set_property (GObject *object,
                                      guint property_id,
                                      const GValue *value,
                                      GParamSpec *pspec)
{
    HazeImChannelFactory *self = HAZE_IM_CHANNEL_FACTORY (object);

    switch (property_id) {
        case PROP_CONNECTION:
            self->priv->conn = g_value_get_object (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
status_changed_cb (HazeConnection *conn,
                   guint status,
                   guint reason,
                   HazeImChannelFactory *self)
{
    if (status == TP_CONNECTION_STATUS_DISCONNECTED)
        close_all (self);
}

static void
haze_im_channel_factory_constructed (GObject *object)
{
    HazeImChannelFactory *self = HAZE_IM_CHANNEL_FACTORY (object);
    void (*constructed) (GObject *) =
        ((GObjectClass *) haze_im_channel_factory_parent_class)->constructed;

    if (constructed != NULL)
    {
        constructed (object);
    }

    self->priv->status_changed_id = g_signal_connect (self->priv->conn,
        "status-changed", (GCallback) status_changed_cb, self);
}

static void
haze_im_channel_factory_class_init (HazeImChannelFactoryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GParamSpec *param_spec;
    void *conv_handle = purple_conversations_get_handle();

    object_class->constructed = haze_im_channel_factory_constructed;
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
    HazeImChannelFactory *self = HAZE_IM_CHANNEL_FACTORY (user_data);
    TpHandle contact_handle;
    guint really_destroyed;

    tp_channel_manager_emit_channel_closed_for_object (self,
        TP_EXPORTABLE_CHANNEL (chan));

    if (self->priv->channels)
    {
        g_object_get (chan,
            "handle", &contact_handle,
            "channel-destroyed", &really_destroyed,
            NULL);

        if (really_destroyed)
        {
            DEBUG ("removing channel with handle %u", contact_handle);
            g_hash_table_remove (self->priv->channels,
                GUINT_TO_POINTER (contact_handle));
        }
        else
        {
            DEBUG ("reopening channel with handle %u due to pending messages",
                contact_handle);
            tp_channel_manager_emit_new_channel (self,
                (TpExportableChannel *) chan, NULL);
        }
    }
}

static HazeIMChannel *
new_im_channel (HazeImChannelFactory *self,
                TpHandle handle,
                TpHandle initiator,
                gpointer request_token)
{
    TpBaseConnection *conn;
    HazeIMChannel *chan;
    char *object_path;
    GSList *requests = NULL;

    g_assert (HAZE_IS_IM_CHANNEL_FACTORY (self));

    conn = (TpBaseConnection *) self->priv->conn;

    g_assert (!g_hash_table_lookup (self->priv->channels,
          GINT_TO_POINTER (handle)));

    object_path = g_strdup_printf ("%s/ImChannel%u", conn->object_path, handle);

    chan = g_object_new (HAZE_TYPE_IM_CHANNEL,
                         "connection", self->priv->conn,
                         "object-path", object_path,
                         "handle", handle,
                         "initiator-handle", initiator,
                         NULL);

    DEBUG ("Created IM channel with object path %s", object_path);

    g_signal_connect (chan, "closed", G_CALLBACK (im_channel_closed_cb), self);

    g_hash_table_insert (self->priv->channels, GINT_TO_POINTER (handle), chan);

    if (request_token != NULL)
        requests = g_slist_prepend (requests, request_token);

    tp_channel_manager_emit_new_channel (self,
        TP_EXPORTABLE_CHANNEL (chan), requests);
    g_slist_free (requests);

    g_free (object_path);

    return chan;
}

static HazeIMChannel *
get_im_channel (HazeImChannelFactory *self,
                TpHandle handle,
                TpHandle initiator,
                gpointer request_token,
                gboolean *created)
{
    HazeIMChannel *chan =
        g_hash_table_lookup (self->priv->channels, GINT_TO_POINTER (handle));

    if (chan)
    {
        if (created)
            *created = FALSE;
    }
    else
    {
        chan = new_im_channel (self, handle, initiator, request_token);
        if (created)
            *created = TRUE;
    }
    g_assert (chan);
    return chan;
}

static void
close_all (HazeImChannelFactory *self)
{
    GHashTable *tmp;

    DEBUG ("closing im channels");

    if (self->priv->channels)
    {
        tmp = self->priv->channels;
        self->priv->channels = NULL;
        g_hash_table_destroy (tmp);
    }

    if (self->priv->status_changed_id != 0)
    {
        g_signal_handler_disconnect (self->priv->conn,
            self->priv->status_changed_id);
        self->priv->status_changed_id = 0;
    }
}

struct _ForeachData
{
    TpExportableChannelFunc foreach;
    gpointer user_data;
};

static void
_foreach_slave (gpointer key, gpointer value, gpointer user_data)
{
    struct _ForeachData *data = (struct _ForeachData *) user_data;
    TpExportableChannel *chan = TP_EXPORTABLE_CHANNEL (value);

    data->foreach (chan, data->user_data);
}

static void
haze_im_channel_factory_foreach (TpChannelManager *iface,
                                 TpExportableChannelFunc foreach,
                                 gpointer user_data)
{
    HazeImChannelFactory *self = HAZE_IM_CHANNEL_FACTORY (iface);
    struct _ForeachData data;

    data.user_data = user_data;
    data.foreach = foreach;

    g_hash_table_foreach (self->priv->channels, _foreach_slave, &data);
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
    HazeConversationUiData *ui_data = PURPLE_CONV_GET_HAZE_UI_DATA (conv);
    HazeIMChannel *chan = get_im_channel (im_factory, ui_data->contact_handle,
        ui_data->contact_handle, NULL, NULL);

    haze_im_channel_receive (chan, xhtml_message, flags, mtime);
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
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (im_factory->priv->conn);
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
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (im_factory->priv->conn);
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

static const gchar * const fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetHandleType",
    NULL
};
static const gchar * const allowed_properties[] = {
    TP_IFACE_CHANNEL ".TargetHandle",
    TP_IFACE_CHANNEL ".TargetID",
    NULL
};

static void
haze_im_channel_factory_foreach_channel_class (TpChannelManager *manager,
    TpChannelManagerChannelClassFunc func,
    gpointer user_data)
{
    GHashTable *table = g_hash_table_new_full (g_str_hash, g_str_equal,
        NULL, (GDestroyNotify) tp_g_value_slice_free);
    GValue *value;

    value = tp_g_value_slice_new (G_TYPE_STRING);
    g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_TEXT);
    g_hash_table_insert (table, TP_IFACE_CHANNEL ".ChannelType", value);

    value = tp_g_value_slice_new (G_TYPE_UINT);
    g_value_set_uint (value, TP_HANDLE_TYPE_CONTACT);
    g_hash_table_insert (table, TP_IFACE_CHANNEL ".TargetHandleType", value);

    func (manager, table, allowed_properties, user_data);

    g_hash_table_destroy (table);
}

static gboolean
haze_im_channel_factory_request (HazeImChannelFactory *self,
                                 gpointer request_token,
                                 GHashTable *request_properties,
                                 gboolean require_new)
{
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (self->priv->conn);
    TpHandle handle;
    gboolean created;
    HazeIMChannel *chan;
    GError *error = NULL;

    if (tp_strdiff (tp_asv_get_string (request_properties,
            TP_IFACE_CHANNEL ".ChannelType"),
        TP_IFACE_CHANNEL_TYPE_TEXT))
    {
        return FALSE;
    }

    if (tp_asv_get_uint32 (request_properties,
        TP_IFACE_CHANNEL ".TargetHandleType", NULL) != TP_HANDLE_TYPE_CONTACT)
    {
        return FALSE;
    }

    handle = tp_asv_get_uint32 (request_properties,
        TP_IFACE_CHANNEL ".TargetHandle", NULL);
    g_assert (handle != 0);

    if (tp_channel_manager_asv_has_unknown_properties (request_properties,
          fixed_properties, allowed_properties, &error))
    {
        goto error;
    }

    chan = get_im_channel (self, handle, base_conn->self_handle,
        request_token, &created);
    g_assert (chan != NULL);

    if (!created)
    {
        if (require_new)
        {
            tp_channel_manager_emit_request_failed (self, request_token,
                TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "Channel already exists");
        }
        else
        {
            tp_channel_manager_emit_request_already_satisfied (self,
                request_token, TP_EXPORTABLE_CHANNEL (chan));
        }
    }

    return TRUE;

error:
    tp_channel_manager_emit_request_failed (self, request_token,
        error->domain, error->code, error->message);
    g_error_free (error);
    return TRUE;
}

static gboolean
haze_im_channel_factory_create_channel (TpChannelManager *manager,
                                        gpointer request_token,
                                        GHashTable *request_properties)
{
    return haze_im_channel_factory_request (HAZE_IM_CHANNEL_FACTORY (manager),
        request_token, request_properties, TRUE);
}

static gboolean
haze_im_channel_factory_ensure_channel (TpChannelManager *manager,
                                        gpointer request_token,
                                        GHashTable *request_properties)
{
    return haze_im_channel_factory_request (HAZE_IM_CHANNEL_FACTORY (manager),
        request_token, request_properties, FALSE);
}

static void
channel_manager_iface_init (gpointer g_iface,
                            gpointer iface_data G_GNUC_UNUSED)
{
    TpChannelManagerIface *iface = g_iface;

    iface->foreach_channel = haze_im_channel_factory_foreach;
    iface->foreach_channel_class =
        haze_im_channel_factory_foreach_channel_class;
    iface->create_channel = haze_im_channel_factory_create_channel;
    iface->ensure_channel = haze_im_channel_factory_ensure_channel;
    /* Request is equivalent to Ensure for this channel class */
    iface->request_channel = haze_im_channel_factory_ensure_channel;
}
