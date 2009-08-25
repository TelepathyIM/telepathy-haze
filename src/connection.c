/*
 * connection.c - HazeConnection source
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

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/dbus-properties-mixin.h>
#include <telepathy-glib/errors.h>
#include <telepathy-glib/handle-repo-dynamic.h>
#include <telepathy-glib/handle-repo-static.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-generic.h>

#include <libpurple/accountopt.h>
#include <libpurple/version.h>

#include "debug.h"
#include "defines.h"
#include "connection-manager.h"
#include "connection.h"
#include "connection-presence.h"
#include "connection-aliasing.h"
#include "connection-avatars.h"
#include "contact-list-channel.h"

enum
{
    PROP_PARAMETERS = 1,
    PROP_PROTOCOL_INFO,

    LAST_PROPERTY
} HazeConnectionProperties;

G_DEFINE_TYPE_WITH_CODE(HazeConnection,
    haze_connection,
    TP_TYPE_BASE_CONNECTION,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
        tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_PRESENCE,
        tp_presence_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_SIMPLE_PRESENCE,
        tp_presence_mixin_simple_presence_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_ALIASING,
        haze_connection_aliasing_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_AVATARS,
        haze_connection_avatars_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_CONTACTS,
        tp_contacts_mixin_iface_init);
    );

typedef struct _HazeConnectionPrivate
{
    GHashTable *parameters;

    HazeProtocolInfo *protocol_info;

    /* Set if purple_account_disconnect has been called or is scheduled to be
     * called, so should not be called again.
     */
    gboolean disconnecting : 1;

    gboolean dispose_has_run : 1;
} HazeConnectionPrivate;

#define HAZE_CONNECTION_GET_PRIVATE(o) \
  ((HazeConnectionPrivate *)o->priv)

#define PC_GET_BASE_CONN(pc) \
    (ACCOUNT_GET_TP_BASE_CONNECTION (purple_connection_get_account (pc)))

static void
connected_cb (PurpleConnection *pc)
{
    TpBaseConnection *base_conn = PC_GET_BASE_CONN (pc);
    HazeConnection *conn = HAZE_CONNECTION (base_conn);
    PurplePluginProtocolInfo *prpl_info = HAZE_CONNECTION_GET_PRPL_INFO (conn);

    if (prpl_info->icon_spec.format != NULL)
    {
        static const gchar *avatar_ifaces[] = {
            TP_IFACE_CONNECTION_INTERFACE_AVATARS,
            NULL };
        tp_base_connection_add_interfaces (base_conn, avatar_ifaces);
    }

    tp_base_connection_change_status (base_conn,
        TP_CONNECTION_STATUS_CONNECTED,
        TP_CONNECTION_STATUS_REASON_REQUESTED);
}

#if PURPLE_VERSION_CHECK(2,3,0)
static void
haze_report_disconnect_reason (PurpleConnection *gc,
                               PurpleConnectionError reason,
                               const char *text)
{
    PurpleAccount *account = purple_connection_get_account (gc);
    HazeConnection *conn = ACCOUNT_GET_HAZE_CONNECTION (account);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE (conn);
    TpBaseConnection *base_conn = ACCOUNT_GET_TP_BASE_CONNECTION (account);

    TpConnectionStatusReason tp_reason;

    /* When a connection error is reported by libpurple, an idle callback to
     * purple_account_disconnect is added.
     */
    priv->disconnecting = TRUE;

    switch (reason)
    {
        case PURPLE_CONNECTION_ERROR_NETWORK_ERROR:
        /* TODO: this isn't the right mapping.  should this map to
         *       NoneSpecified?
         */
        case PURPLE_CONNECTION_ERROR_OTHER_ERROR:
            tp_reason = TP_CONNECTION_STATUS_REASON_NETWORK_ERROR;
            break;
        case PURPLE_CONNECTION_ERROR_AUTHENTICATION_FAILED:
        case PURPLE_CONNECTION_ERROR_INVALID_USERNAME:
        /* TODO: the following don't really match the tp reason but it's
         *       the nearest match.  Invalid settings shouldn't exist in the
         *       first place.
         */
        case PURPLE_CONNECTION_ERROR_AUTHENTICATION_IMPOSSIBLE:
        case PURPLE_CONNECTION_ERROR_INVALID_SETTINGS:
            tp_reason = TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED;
            break;
        case PURPLE_CONNECTION_ERROR_NO_SSL_SUPPORT:
        case PURPLE_CONNECTION_ERROR_ENCRYPTION_ERROR:
            tp_reason = TP_CONNECTION_STATUS_REASON_ENCRYPTION_ERROR;
            break;
        case PURPLE_CONNECTION_ERROR_NAME_IN_USE:
            tp_reason = TP_CONNECTION_STATUS_REASON_NAME_IN_USE;
            break;
        case PURPLE_CONNECTION_ERROR_CERT_NOT_PROVIDED:
            tp_reason = TP_CONNECTION_STATUS_REASON_CERT_NOT_PROVIDED;
            break;
        case PURPLE_CONNECTION_ERROR_CERT_UNTRUSTED:
            tp_reason = TP_CONNECTION_STATUS_REASON_CERT_UNTRUSTED;
            break;
        case PURPLE_CONNECTION_ERROR_CERT_EXPIRED:
            tp_reason = TP_CONNECTION_STATUS_REASON_CERT_EXPIRED;
            break;
        case PURPLE_CONNECTION_ERROR_CERT_NOT_ACTIVATED:
            tp_reason = TP_CONNECTION_STATUS_REASON_CERT_NOT_ACTIVATED;
            break;
        case PURPLE_CONNECTION_ERROR_CERT_HOSTNAME_MISMATCH:
            tp_reason = TP_CONNECTION_STATUS_REASON_CERT_HOSTNAME_MISMATCH;
            break;
        case PURPLE_CONNECTION_ERROR_CERT_FINGERPRINT_MISMATCH:
            tp_reason = TP_CONNECTION_STATUS_REASON_CERT_FINGERPRINT_MISMATCH;
            break;
        case PURPLE_CONNECTION_ERROR_CERT_SELF_SIGNED:
            tp_reason = TP_CONNECTION_STATUS_REASON_CERT_SELF_SIGNED;
            break;
        case PURPLE_CONNECTION_ERROR_CERT_OTHER_ERROR:
            tp_reason = TP_CONNECTION_STATUS_REASON_CERT_OTHER_ERROR;
            break;
        default:
            g_warning ("report_disconnect_cb: "
                       "invalid PurpleDisconnectReason %u", reason);
            tp_reason = TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED;
    }

    tp_base_connection_change_status (base_conn,
            TP_CONNECTION_STATUS_DISCONNECTED, tp_reason);
}
#endif

static gboolean
idle_finish_shutdown (gpointer data)
{
  tp_base_connection_finish_shutdown (TP_BASE_CONNECTION (data));
  return FALSE;
}

static void
disconnected_cb (PurpleConnection *pc)
{
    PurpleAccount *account = purple_connection_get_account (pc);
    HazeConnection *conn = ACCOUNT_GET_HAZE_CONNECTION (account);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE (conn);
    TpBaseConnection *base_conn = ACCOUNT_GET_TP_BASE_CONNECTION (account);

    priv->disconnecting = TRUE;

    if(base_conn->status != TP_CONNECTION_STATUS_DISCONNECTED)
    {
        tp_base_connection_change_status (base_conn,
            TP_CONNECTION_STATUS_DISCONNECTED,
/* If we have report_disconnect_reason, then if status is not already
 * DISCONNECTED we know that it was requested.  If not, we have no idea.
 */
#if PURPLE_VERSION_CHECK(2,3,0)
            TP_CONNECTION_STATUS_REASON_REQUESTED
#else
            TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED
#endif
            );

    }

    /* Call tp_base_connection_finish_shutdown () in an idle because calling it
     * might lead to the HazeConnection being destroyed, which would mean the
     * PurpleAccount were destroyed, but we're currently inside libpurple code
     * that uses it.
     */
    g_idle_add (idle_finish_shutdown, conn);
}

static void
_warn_unhandled_parameter (const gchar *key,
                           const GValue *value,
                           gpointer user_data)
{
    g_warning ("received an unknown parameter '%s'; ignoring", key);
}

struct _i_want_closure
{
    PurpleAccount *account;
    GHashTable *params;
};

static void
_set_option (const PurpleAccountOption *option,
             struct _i_want_closure *context)
{
    GValue *value = g_hash_table_lookup (context->params, option->pref_name);
    if (!value)
        return;

    switch (option->type)
    {
        case PURPLE_PREF_BOOLEAN:
            g_assert (G_VALUE_TYPE (value) == G_TYPE_BOOLEAN);
            purple_account_set_bool (context->account, option->pref_name,
                g_value_get_boolean (value));
            break;
        case PURPLE_PREF_INT:
            g_assert (G_VALUE_TYPE (value) == G_TYPE_INT);
            purple_account_set_int (context->account, option->pref_name,
                g_value_get_int (value));
            break;
        case PURPLE_PREF_STRING:
        case PURPLE_PREF_STRING_LIST:
            g_assert (G_VALUE_TYPE (value) == G_TYPE_STRING);
            purple_account_set_string (context->account, option->pref_name,
                g_value_get_string (value));
            break;
        default:
            g_warning ("option '%s' has unhandled type %u",
                option->pref_name, option->type);
    }

    g_hash_table_remove (context->params, option->pref_name);
}

/**
 * haze_connection_create_account:
 *
 * Attempts to create a PurpleAccount corresponding to this connection. Must be
 * called immediately after constructing a connection. It's a shame GObject
 * constructors can't fail.
 *
 * Returns: %TRUE if the account was successfully created and hooked up;
 *          %FALSE with @error set in the TP_ERRORS domain if the account
 *          already existed or another error occurred.
 */
gboolean
haze_connection_create_account (HazeConnection *self,
                                GError **error)
{
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE(self);
    GHashTable *params = priv->parameters;
    PurplePluginProtocolInfo *prpl_info = priv->protocol_info->prpl_info;
    const gchar *prpl_id = priv->protocol_info->prpl_id;
    const gchar *username, *password;
    struct _i_want_closure context;

    g_return_val_if_fail (self->account == NULL, FALSE);

    username = tp_asv_get_string (params, "account");
    g_assert (username != NULL);

    if (purple_accounts_find (username, prpl_id) != NULL)
      {
        g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
            "a connection already exists to %s on %s", username, prpl_id);
        return FALSE;
      }

    self->account = purple_account_new (username, priv->protocol_info->prpl_id);
    purple_accounts_add (self->account);
    g_hash_table_remove (params, "account");

    self->account->ui_data = self;

    password = tp_asv_get_string (params, "password");
    if (password)
    {
        purple_account_set_password (self->account, password);
        g_hash_table_remove (params, "password");
    }

    context.account = self->account;
    context.params = params;
    g_list_foreach (prpl_info->protocol_options, (GFunc) _set_option, &context);

    g_hash_table_foreach (params, (GHFunc) _warn_unhandled_parameter, "lala");

    return TRUE;
}

static gboolean
_haze_connection_start_connecting (TpBaseConnection *base,
                                   GError **error)
{
    HazeConnection *self = HAZE_CONNECTION(base);
    TpHandleRepoIface *contact_handles =
        tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);

    g_return_val_if_fail (self->account != NULL, FALSE);

    base->self_handle = tp_handle_ensure (contact_handles,
        purple_account_get_username (self->account), NULL, error);
    if (!base->self_handle)
        return FALSE;

    tp_base_connection_change_status(base, TP_CONNECTION_STATUS_CONNECTING,
                                     TP_CONNECTION_STATUS_REASON_REQUESTED);

    purple_account_set_enabled(self->account, UI_ID, TRUE);
    purple_account_connect(self->account);

    return TRUE;
}

static void
_haze_connection_shut_down (TpBaseConnection *base)
{
    HazeConnection *self = HAZE_CONNECTION(base);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE (self);
    if(!priv->disconnecting)
    {
        priv->disconnecting = TRUE;
        purple_account_disconnect(self->account);
    }
}

/* Must be in the same order as HazeListHandle in connection.h */
static const char *list_handle_strings[] =
{
    "subscribe",    /* HAZE_LIST_HANDLE_SUBSCRIBE */
    "publish",      /* HAZE_LIST_HANDLE_PUBLISH */
#if 0
    "hide",         /* HAZE_LIST_HANDLE_HIDE */
    "allow",        /* HAZE_LIST_HANDLE_ALLOW */
    "deny"          /* HAZE_LIST_HANDLE_DENY */
#endif
    NULL
};

static gchar*
_contact_normalize (TpHandleRepoIface *repo,
                    const gchar *id,
                    gpointer context,
                    GError **error)
{
    HazeConnection *conn = HAZE_CONNECTION (context);
    PurpleAccount *account = conn->account;
    return g_strdup (purple_normalize (account, id));
}

static void
_haze_connection_create_handle_repos (TpBaseConnection *base,
        TpHandleRepoIface *repos[NUM_TP_HANDLE_TYPES])
{
    repos[TP_HANDLE_TYPE_CONTACT] =
        tp_dynamic_handle_repo_new (TP_HANDLE_TYPE_CONTACT, _contact_normalize,
                                    base);
    /* repos[TP_HANDLE_TYPE_ROOM] = XXX MUC */
    repos[TP_HANDLE_TYPE_GROUP] =
        tp_dynamic_handle_repo_new (TP_HANDLE_TYPE_GROUP, NULL, NULL);
    repos[TP_HANDLE_TYPE_LIST] =
        tp_static_handle_repo_new (TP_HANDLE_TYPE_LIST, list_handle_strings);
}

static GPtrArray *
_haze_connection_create_channel_managers (TpBaseConnection *base)
{
    HazeConnection *self = HAZE_CONNECTION(base);
    GPtrArray *channel_managers = g_ptr_array_new ();

    self->im_factory = HAZE_IM_CHANNEL_FACTORY (
        g_object_new (HAZE_TYPE_IM_CHANNEL_FACTORY, "connection", self, NULL));
    g_ptr_array_add (channel_managers, self->im_factory);

    self->contact_list = HAZE_CONTACT_LIST (
        g_object_new (HAZE_TYPE_CONTACT_LIST, "connection", self, NULL));
    g_ptr_array_add (channel_managers, self->contact_list);

    return channel_managers;
}

static gchar *
haze_connection_get_unique_connection_name(TpBaseConnection *base)
{
    HazeConnection *self = HAZE_CONNECTION(base);

    return g_strdup (purple_account_get_username (self->account));
}

static void
haze_connection_get_property (GObject    *object,
                              guint       property_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    HazeConnection *self = HAZE_CONNECTION (object);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE(self);

    switch (property_id) {
        case PROP_PARAMETERS:
            g_value_set_pointer (value, priv->parameters);
            break;
        case PROP_PROTOCOL_INFO:
            g_value_set_pointer (value, priv->protocol_info);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
haze_connection_set_property (GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    HazeConnection *self = HAZE_CONNECTION (object);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE(self);

    switch (property_id) {
        case PROP_PARAMETERS:
            priv->parameters = g_value_get_pointer (value);
            g_hash_table_ref (priv->parameters);
            break;
        case PROP_PROTOCOL_INFO:
            priv->protocol_info = g_value_get_pointer (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static GObject *
haze_connection_constructor (GType type,
                             guint n_construct_properties,
                             GObjectConstructParam *construct_params)
{
    HazeConnection *self = HAZE_CONNECTION (
            G_OBJECT_CLASS (haze_connection_parent_class)->constructor (
                type, n_construct_properties, construct_params));
    GObject *object = (GObject *) self;
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE (self);

    DEBUG ("Post-construction: (HazeConnection *)%p", self);

    self->acceptable_avatar_mime_types = NULL;

    priv->dispose_has_run = FALSE;

    priv->disconnecting = FALSE;

    tp_contacts_mixin_init (object,
        G_STRUCT_OFFSET (HazeConnection, contacts));
    tp_base_connection_register_with_contacts_mixin (
        TP_BASE_CONNECTION (self));

    haze_connection_aliasing_init (object);
    haze_connection_avatars_init (object);
    haze_connection_presence_init (object);

    return (GObject *)self;
}

static void
haze_connection_dispose (GObject *object)
{
    HazeConnection *self = HAZE_CONNECTION(object);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE (self);

    if (priv->dispose_has_run)
        return;

    priv->dispose_has_run = TRUE;

    DEBUG ("disposing of (HazeConnection *)%p", self);

    g_hash_table_unref (priv->parameters);
    priv->parameters = NULL;

    G_OBJECT_CLASS (haze_connection_parent_class)->dispose (object);
}

static void
haze_connection_finalize (GObject *object)
{
    HazeConnection *self = HAZE_CONNECTION (object);

    tp_contacts_mixin_finalize (object);
    tp_presence_mixin_finalize (object);

    g_strfreev (self->acceptable_avatar_mime_types);

    if (self->account != NULL)
      {
        DEBUG ("deleting account %s", self->account->username);
        purple_accounts_delete (self->account);
      }

    G_OBJECT_CLASS (haze_connection_parent_class)->finalize (object);
}

static void
haze_connection_class_init (HazeConnectionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    TpBaseConnectionClass *base_class = TP_BASE_CONNECTION_CLASS (klass);
    GParamSpec *param_spec;
    static const gchar *interfaces_always_present[] = {
        TP_IFACE_CONNECTION_INTERFACE_REQUESTS,
        TP_IFACE_CONNECTION_INTERFACE_PRESENCE,
        TP_IFACE_CONNECTION_INTERFACE_SIMPLE_PRESENCE,
        TP_IFACE_CONNECTION_INTERFACE_CONTACTS,
        /* TODO: This is a lie.  Not all protocols supported by libpurple
         *       actually have the concept of a user-settable alias, but
         *       there's no way for the UI to know (yet).
         */
        TP_IFACE_CONNECTION_INTERFACE_ALIASING,
        NULL };

    DEBUG ("Initializing (HazeConnectionClass *)%p", klass);

    g_type_class_add_private (klass, sizeof (HazeConnectionPrivate));
    object_class->get_property = haze_connection_get_property;
    object_class->set_property = haze_connection_set_property;
    object_class->constructor = haze_connection_constructor;
    object_class->dispose = haze_connection_dispose;
    object_class->finalize = haze_connection_finalize;

    base_class->create_handle_repos = _haze_connection_create_handle_repos;
    base_class->create_channel_managers =
        _haze_connection_create_channel_managers;
    base_class->get_unique_connection_name =
        haze_connection_get_unique_connection_name;
    base_class->start_connecting = _haze_connection_start_connecting;
    base_class->shut_down = _haze_connection_shut_down;
    base_class->interfaces_always_present = interfaces_always_present;

    param_spec = g_param_spec_pointer ("parameters",
                                       "GHashTable of gchar * => GValue",
                                       "Connection parameters (username, "
                                       "password, etc.)",
                                       G_PARAM_CONSTRUCT_ONLY |
                                       G_PARAM_READWRITE |
                                       G_PARAM_STATIC_NAME |
                                       G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_PARAMETERS, param_spec);

    param_spec = g_param_spec_pointer ("protocol-info",
                                       "HazeProtocolInfo instance",
                                       "Information on how this protocol "
                                       "should be treated by haze",
                                       G_PARAM_CONSTRUCT_ONLY |
                                       G_PARAM_READWRITE |
                                       G_PARAM_STATIC_NAME |
                                       G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_PROTOCOL_INFO,
                                     param_spec);

    tp_dbus_properties_mixin_class_init (object_class, 0);

    tp_contacts_mixin_class_init (object_class,
        G_STRUCT_OFFSET (HazeConnectionClass, contacts_class));

    haze_connection_presence_class_init (object_class);
    haze_connection_aliasing_class_init (object_class);
    haze_connection_avatars_class_init (object_class);
}

static void
haze_connection_init (HazeConnection *self)
{
    DEBUG ("Initializing (HazeConnection *)%p", self);
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, HAZE_TYPE_CONNECTION,
                                              HazeConnectionPrivate);
}

static PurpleAccountUiOps
account_ui_ops =
{
    NULL,                                            /* notify_added */
    haze_connection_presence_account_status_changed, /* status_changed */
    NULL,                                            /* request_add */
    haze_request_authorize,                          /* request_authorize */
    haze_close_account_request,                      /* close_account_request */

    NULL, /* purple_reserved1 */
    NULL, /* purple_reserved2 */
    NULL, /* purple_reserved3 */
    NULL  /* purple_reserved4 */
};

PurpleAccountUiOps *
haze_get_account_ui_ops ()
{
    return &account_ui_ops;
}

static PurpleConnectionUiOps
connection_ui_ops =
{
    NULL,            /* connect_progress */
    connected_cb,    /* connected */
    disconnected_cb, /* disconnected */
    NULL,            /* notice */
    NULL,            /* report_disconnect */
    NULL,            /* network_connected */
    NULL,            /* network_disconnected */
#if PURPLE_VERSION_CHECK(2,3,0)
    haze_report_disconnect_reason, /* report_disconnect_reason */
#else
    NULL, /* _purple_reserved0 */
#endif

    NULL, /* _purple_reserved1 */
    NULL, /* _purple_reserved2 */
    NULL  /* _purple_reserved3 */
};

PurpleConnectionUiOps *
haze_get_connection_ui_ops ()
{
    return &connection_ui_ops;
}

const gchar *
haze_connection_handle_inspect (HazeConnection *conn,
                                TpHandleType handle_type,
                                TpHandle handle)
{
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
    TpHandleRepoIface *handle_repo =
        tp_base_connection_get_handles (base_conn, handle_type);
    g_assert (tp_handle_is_valid (handle_repo, handle, NULL));
    return tp_handle_inspect (handle_repo, handle);
}
