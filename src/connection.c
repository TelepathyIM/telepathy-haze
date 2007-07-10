#include <telepathy-glib/handle-repo-dynamic.h>
#include <telepathy-glib/handle-repo-static.h>
#include <telepathy-glib/errors.h>

#include "defines.h"
#include "connection.h"
#include "im-channel-factory.h"
#include "contact-list.h"

enum
{
    PROP_USERNAME = 1,
    PROP_PASSWORD,
    PROP_SERVER,

    LAST_PROPERTY
};

G_DEFINE_TYPE(HazeConnection,
    haze_connection,
    TP_TYPE_BASE_CONNECTION);

typedef struct _HazeConnectionPrivate
{
    char *username;
    char *password;
    char *server;
} HazeConnectionPrivate;

#define HAZE_CONNECTION_GET_PRIVATE(o) \
  ((HazeConnectionPrivate *)o->priv)

void
signed_on_cb (PurpleConnection *pc, HazeConnection *self)
{
    if (self->account != purple_connection_get_account (pc))
        return;

    tp_base_connection_change_status(
        TP_BASE_CONNECTION(self), TP_CONNECTION_STATUS_CONNECTED,
        TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);
}

void
signing_off_cb (PurpleConnection *pc, HazeConnection *self)
{
    if (self->account != purple_connection_get_account (pc))
        return;

    /* XXX Notify with the reason! */
    if(TP_BASE_CONNECTION(self)->status != TP_CONNECTION_STATUS_DISCONNECTED) {
        tp_base_connection_change_status(
            TP_BASE_CONNECTION(self), TP_CONNECTION_STATUS_DISCONNECTED,
            TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);
    }
}

static gboolean
idle_finish_shutdown_cb(gpointer data)
{
    TpBaseConnection *base = TP_BASE_CONNECTION(data);
    tp_base_connection_finish_shutdown(base);
    return FALSE;
}

void
signed_off_cb (PurpleConnection *pc, HazeConnection *self)
{
    if (self->account != purple_connection_get_account (pc))
        return;

    g_idle_add(idle_finish_shutdown_cb, self);
}

static gboolean
_haze_connection_start_connecting (TpBaseConnection *base,
                                   GError **error)
{
    HazeConnection *self = HAZE_CONNECTION(base);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE(self);
    char *protocol, *password, *prpl;
    TpHandleRepoIface *contact_handles =
        tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);

    g_object_get(G_OBJECT(self),
                 "protocol", &protocol,
                 "password", &password,
                 NULL);

    base->self_handle = tp_handle_ensure(contact_handles, priv->username,
                                         NULL, error);

    prpl = g_strconcat("prpl-", protocol, NULL);
    self->account = purple_account_new(priv->username, prpl);
    g_free(prpl);

    purple_account_set_password(self->account, password);
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

    g_ptr_array_add (channel_factories,
                     g_object_new (HAZE_TYPE_IM_CHANNEL_FACTORY,
                                   "connection", self,
                                   NULL));

    g_ptr_array_add (channel_factories,
                     g_object_new (HAZE_TYPE_CONTACT_LIST,
                                   "connection", self,
                                   NULL));

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
    purple_signal_connect(purple_connections_get_handle(), "signed-on",
                          self, PURPLE_CALLBACK(signed_on_cb), self);
    purple_signal_connect(purple_connections_get_handle(), "signing-off",
                          self, PURPLE_CALLBACK(signing_off_cb), self);
    purple_signal_connect(purple_connections_get_handle(), "signed-off",
                          self, PURPLE_CALLBACK(signed_off_cb), self);


    return (GObject *)self;
}

static void
haze_connection_dispose (GObject *object)
{
    HazeConnection *self = HAZE_CONNECTION(object);

    g_debug("disposing of (HazeConnection *)%p", self);

    if(self->account != NULL) {
        purple_accounts_delete(self->account);
        self->account = NULL;

        purple_signals_disconnect_by_handle(self);
    }

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

    G_OBJECT_CLASS (haze_connection_parent_class)->finalize (object);
}

static void
haze_connection_class_init (HazeConnectionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    TpBaseConnectionClass *base_class = TP_BASE_CONNECTION_CLASS (klass);
    GParamSpec *param_spec;

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

    param_spec = g_param_spec_string ("username", "Account username",
                                      "The username used when authenticating.",
                                      NULL,
                                      G_PARAM_CONSTRUCT_ONLY |
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NAME |
                                      G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_USERNAME, param_spec);

    param_spec = g_param_spec_string ("password", "Account password",
                                      "The protocol to which to connect.",
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
}

static void
haze_connection_init (HazeConnection *self)
{
    g_debug("Initializing (HazeConnection *)%p", self);
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, HAZE_TYPE_CONNECTION,
                                              HazeConnectionPrivate);
}
