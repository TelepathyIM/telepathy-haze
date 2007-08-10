/*
 * connection.c - HazeConnection source
 * Copyright (C) 2007 Will Thompson
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

#include <telepathy-glib/handle-repo-dynamic.h>
#include <telepathy-glib/handle-repo-static.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/errors.h>

#include <accountopt.h>
#include <version.h>

#include "defines.h"
#include "connection.h"
#include "connection-presence.h"
#include "connection-aliasing.h"
#include "connection-avatars.h"

enum
{
    PROP_USERNAME = 1,
    PROP_PASSWORD,
    PROP_SERVER,

    LAST_PROPERTY
};

G_DEFINE_TYPE_WITH_CODE(HazeConnection,
    haze_connection,
    TP_TYPE_BASE_CONNECTION,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_PRESENCE,
        tp_presence_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_ALIASING,
        haze_connection_aliasing_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_AVATARS,
        haze_connection_avatars_iface_init);
    );

typedef struct _HazeConnectionPrivate
{
    char *username;
    char *password;
    char *server;
} HazeConnectionPrivate;

#define HAZE_CONNECTION_GET_PRIVATE(o) \
  ((HazeConnectionPrivate *)o->priv)

#define PC_GET_BASE_CONN(pc) \
    (ACCOUNT_GET_TP_BASE_CONNECTION (purple_connection_get_account (pc)))

void
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
        TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);
}

static void
report_disconnect_cb (PurpleConnection *gc,
                      const char *text)
{
    /* FIXME: Actually report the reason to tp_base_connection_change_status */
    g_debug ("report_disconnect_cb: %s", text);
}

static gboolean
idle_disconnected_cb(gpointer data)
{
    PurpleAccount *account = (PurpleAccount *) data;
    HazeConnection *conn = ACCOUNT_GET_HAZE_CONNECTION (account);

    g_debug ("deleting account %s", account->username);
    purple_accounts_delete (account);
    tp_base_connection_finish_shutdown (TP_BASE_CONNECTION (conn));
    return FALSE;
}

void
disconnected_cb (PurpleConnection *pc)
{
    PurpleAccount *account = purple_connection_get_account (pc);
    TpBaseConnection *base_conn = ACCOUNT_GET_TP_BASE_CONNECTION (account);

    if(base_conn->status != TP_CONNECTION_STATUS_DISCONNECTED)
    {
        tp_base_connection_change_status (base_conn,
            TP_CONNECTION_STATUS_DISCONNECTED,
            TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);
    }

    g_idle_add(idle_disconnected_cb, account);
}

static gboolean
_haze_connection_start_connecting (TpBaseConnection *base,
                                   GError **error)
{
    HazeConnection *self = HAZE_CONNECTION(base);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE(self);
    char *protocol, *password, *server, *prpl_id;
    PurpleAccount *account;
    PurplePlugin *prpl;
    PurplePluginProtocolInfo *prpl_info;
    TpHandleRepoIface *contact_handles =
        tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);

    g_object_get(G_OBJECT(self),
                 "protocol", &protocol,
                 "password", &password,
                 "server", &server,
                 NULL);

    base->self_handle = tp_handle_ensure(contact_handles, priv->username,
                                         NULL, error);

    prpl_id = g_strconcat("prpl-", protocol, NULL);
    prpl = purple_find_prpl (prpl_id);
    g_assert (prpl);
    account = self->account = purple_account_new(priv->username, prpl_id);
    g_free(prpl_id);

    account->ui_data = self;
    purple_account_set_password (account, password);
    if (server && *server)
    {
        GList *l;
        PurpleAccountOption *option;
        prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO (prpl);

        /* :'-( :'-( :'-( :'-( */
        for (l = prpl_info->protocol_options; l != NULL; l = l->next)
        {
            option = (PurpleAccountOption *)l->data;
            if (!strcmp (option->pref_name, "server") /* oscar */
                || !strcmp (option->pref_name, "connect_server")) /* xmpp */
            {
                purple_account_set_string (account, option->pref_name, server);
                break;
            }
        }
        if (l == NULL)
            g_warning ("server protocol option not found!");
    }
    purple_account_set_enabled(self->account, UI_ID, TRUE);
    purple_account_connect(self->account);

    tp_base_connection_change_status(base, TP_CONNECTION_STATUS_CONNECTING,
                                     TP_CONNECTION_STATUS_REASON_REQUESTED);

    return TRUE;
}

static void
_haze_connection_shut_down (TpBaseConnection *base)
{
    HazeConnection *self = HAZE_CONNECTION(base);
    if(!self->account->disconnecting)
        purple_account_disconnect(self->account);
}

/* Must be in the same order as HazeListHandle in connection.h */
static const char *list_handle_strings[] =
{
    "subscribe",    /* HAZE_LIST_HANDLE_SUBSCRIBE */
#if 0
    "publish",      /* HAZE_LIST_HANDLE_PUBLISH */
    "known",        /* HAZE_LIST_HANDLE_KNOWN */
    "deny",         /* HAZE_LIST_HANDLE_DENY */
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
_haze_connection_create_channel_factories (TpBaseConnection *base)
{
    HazeConnection *self = HAZE_CONNECTION(base);
    GPtrArray *channel_factories = g_ptr_array_new ();

    self->im_factory = HAZE_IM_CHANNEL_FACTORY (
        g_object_new (HAZE_TYPE_IM_CHANNEL_FACTORY, "connection", self, NULL));
    g_ptr_array_add (channel_factories, self->im_factory);

    self->contact_list = HAZE_CONTACT_LIST (
        g_object_new (HAZE_TYPE_CONTACT_LIST, "connection", self, NULL));
    g_ptr_array_add (channel_factories, self->contact_list);

    return channel_factories;
}

gchar *
haze_connection_get_unique_connection_name(TpBaseConnection *base)
{
    HazeConnection *self = HAZE_CONNECTION(base);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE(self);

    return g_strdup(priv->username);
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
        case PROP_USERNAME:
            g_value_set_string (value, priv->username);
            break;
        case PROP_PASSWORD:
            g_value_set_string (value, priv->password);
            break;
        case PROP_SERVER:
            g_value_set_string (value, priv->server);
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
        case PROP_USERNAME:
            g_free (priv->username);
            priv->username = g_value_dup_string(value);
            break;
        case PROP_PASSWORD:
            g_free (priv->password);
            priv->password = g_value_dup_string(value);
            break;
        case PROP_SERVER:
            g_free (priv->server);
            priv->server = g_value_dup_string(value);
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

    g_debug("Post-construction: (HazeConnection *)%p", self);


    return (GObject *)self;
}

static void
haze_connection_dispose (GObject *object)
{
    HazeConnection *self = HAZE_CONNECTION(object);

    g_debug("disposing of (HazeConnection *)%p", self);

    G_OBJECT_CLASS (haze_connection_parent_class)->dispose (object);
}

static void
haze_connection_finalize (GObject *object)
{
    HazeConnection *self = HAZE_CONNECTION (object);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE(self);

    g_free (priv->username);
    g_free (priv->password);
    g_free (priv->server);
    self->priv = NULL;

    tp_presence_mixin_finalize (object);

    G_OBJECT_CLASS (haze_connection_parent_class)->finalize (object);
}

static void
haze_connection_class_init (HazeConnectionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    TpBaseConnectionClass *base_class = TP_BASE_CONNECTION_CLASS (klass);
    GParamSpec *param_spec;
    static const gchar *interfaces_always_present[] = {
        TP_IFACE_CONNECTION_INTERFACE_PRESENCE,
        TP_IFACE_CONNECTION_INTERFACE_ALIASING, /* FIXME: I'm lying */
        NULL };

    g_debug("Initializing (HazeConnectionClass *)%p", klass);

    g_type_class_add_private (klass, sizeof (HazeConnectionPrivate));
    object_class->get_property = haze_connection_get_property;
    object_class->set_property = haze_connection_set_property;
    object_class->constructor = haze_connection_constructor;
    object_class->dispose = haze_connection_dispose;
    object_class->finalize = haze_connection_finalize;

    base_class->create_handle_repos = _haze_connection_create_handle_repos;
    base_class->create_channel_factories =
        _haze_connection_create_channel_factories;
    base_class->get_unique_connection_name =
        haze_connection_get_unique_connection_name;
    base_class->start_connecting = _haze_connection_start_connecting;
    base_class->shut_down = _haze_connection_shut_down;
    base_class->interfaces_always_present = interfaces_always_present;

    param_spec = g_param_spec_string ("username", "Account username",
                                      "The username used when authenticating.",
                                      NULL,
                                      G_PARAM_CONSTRUCT_ONLY |
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NAME |
                                      G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_USERNAME, param_spec);

    param_spec = g_param_spec_string ("password", "Account password",
                                      "The password used when authenticating.",
                                      NULL,
                                      G_PARAM_CONSTRUCT_ONLY |
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NAME |
                                      G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_PASSWORD, param_spec);

    param_spec = g_param_spec_string ("server", "Hostname or IP of server",
                                      "The server used when establishing a connection.",
                                      NULL,
                                      G_PARAM_CONSTRUCT_ONLY |
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NAME |
                                      G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_SERVER, param_spec);

    haze_connection_presence_class_init (object_class);
    haze_connection_aliasing_class_init (object_class);
    haze_connection_avatars_class_init (object_class);
}

static void
haze_connection_init (HazeConnection *self)
{
    g_debug("Initializing (HazeConnection *)%p", self);
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, HAZE_TYPE_CONNECTION,
                                              HazeConnectionPrivate);

    haze_connection_presence_init (self);
}

/* Without the ifdef check, this compiles with warnings.  Except I want
 * -Werror, so...
 */
static void *
request_authorize_cb (PurpleAccount *account,
                      const char *remote_user,
                      const char *id,
                      const char *alias,
                      const char *message,
                      gboolean on_list,
#if PURPLE_VERSION_CHECK(2,1,1)
                      PurpleAccountRequestAuthorizationCb authorize_cb,
                      PurpleAccountRequestAuthorizationCb deny_cb,
#else
                      GCallback authorize_cb,
                      GCallback deny_cb,
#endif
                      void *user_data)
{
    /* Woo for argument lists which are longer than the function! */
    PurpleAccountRequestAuthorizationCb cb =
#if PURPLE_VERSION_CHECK(2,1,1)
        authorize_cb;
#else
        (PurpleAccountRequestAuthorizationCb) authorize_cb;
#endif

    /* FIXME: Implement the publish list, then deal with this properly. */
    g_debug ("[%s] Quietly authorizing presence subscription from '%s'...",
             account->username, remote_user);
    cb (user_data);
    return NULL;
}

static PurpleAccountUiOps
account_ui_ops =
{
    NULL,                                            /* notify_added */
    haze_connection_presence_account_status_changed, /* status_changed */
    NULL,                                            /* request_add */
    request_authorize_cb,                            /* request_authorize */
    NULL,                                            /* close_account_request */

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
    report_disconnect_cb, /* report_disconnect */
    NULL,            /* network_connected */
    NULL,            /* network_disconnected */

    NULL, /* _purple_reserved1 */
    NULL, /* _purple_reserved2 */
    NULL, /* _purple_reserved3 */
    NULL  /* _purple_reserved4 */
};

PurpleConnectionUiOps *
haze_get_connection_ui_ops ()
{
    return &connection_ui_ops;
}
