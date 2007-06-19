#include <savedstatuses.h>

#include <telepathy-glib/handle-repo-dynamic.h>
#include <telepathy-glib/errors.h>

#include "defines.h"
#include "connection.h"

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

void
haze_connection_signed_on_cb (HazeConnection *conn)
{
    PurpleAccount *account = conn->account;
    printf("Account connected: %s %s\n", account->username, account->protocol_id);

    tp_base_connection_change_status(
        TP_BASE_CONNECTION(conn), TP_CONNECTION_STATUS_CONNECTED,
        TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);
}

void
haze_connection_signing_off_cb (HazeConnection *conn)
{
    tp_base_connection_change_status(
        TP_BASE_CONNECTION(conn), TP_CONNECTION_STATUS_DISCONNECTED,
        TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);
}

static gboolean
_haze_connection_start_connecting (TpBaseConnection *base,
                                   GError **error)
{
    HazeConnection *self = HAZE_CONNECTION(base);
    char *protocol, *password, *prpl, *id;
    TpHandleRepoIface *contact_handles =
        tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);

    g_object_get(G_OBJECT(self),
                 "protocol", &protocol,
                 "password", &password,
                 NULL);

    id = g_strdup_printf("%s:%s", protocol, self->username);
    base->self_handle = tp_handle_ensure(contact_handles, id, NULL, error);
    g_free(id);

    prpl = g_strconcat("prpl-", protocol, NULL);
    self->account = purple_account_new(self->username, prpl);
    g_free(prpl);

    purple_account_set_password(self->account, password);
    purple_account_set_enabled(self->account, UI_ID, TRUE);

    PurpleSavedStatus *status = purple_savedstatus_new(NULL, PURPLE_STATUS_AVAILABLE);
    purple_savedstatus_activate(status);

    tp_base_connection_change_status(base, TP_CONNECTION_STATUS_CONNECTING,
                                     TP_CONNECTION_STATUS_REASON_REQUESTED);

    return TRUE;
}

static void
_haze_connection_shut_down (TpBaseConnection *base)
{
    HazeConnection *self = HAZE_CONNECTION(base);

    purple_account_set_enabled(self->account, UI_ID, FALSE);
}

static void
_haze_connection_create_handle_repos (TpBaseConnection *conn,
        TpHandleRepoIface *repos[NUM_TP_HANDLE_TYPES])
{
    /* FIXME: Should probably normalize... */
    repos[TP_HANDLE_TYPE_CONTACT] =
        tp_dynamic_handle_repo_new (TP_HANDLE_TYPE_CONTACT, NULL, NULL);
    /* FIXME:
    repos[TP_HANDLE_TYPE_ROOM]
    repos[TP_HANDLE_TYPE_GROUP]
    repos[TP_HANDLE_TYPE_LIST]
    */
}

static GPtrArray *
_haze_connection_create_channel_factories (TpBaseConnection *conn)
{
    return g_ptr_array_new ();
}

gchar *
haze_connection_get_unique_connection_name(TpBaseConnection *base)
{
    HazeConnection *conn = HAZE_CONNECTION(base);
    gchar *protocol, *conn_name;

    g_object_get(G_OBJECT(conn),
                 "protocol", &protocol,
                 NULL);

    conn_name = g_strdup(conn->username);
    return conn_name;
}

static void
haze_connection_get_property (GObject    *object,
                              guint       property_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    HazeConnection *self = HAZE_CONNECTION (object);

    switch (property_id) {
        case PROP_USERNAME:
            g_value_set_string (value, self->username);
            break;
        case PROP_PASSWORD:
            g_value_set_string (value, self->password);
            break;
        case PROP_SERVER:
            g_value_set_string (value, self->server);
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

    switch (property_id) {
        case PROP_USERNAME:
            g_free (self->username);
            self->username = g_value_dup_string(value);
            break;
        case PROP_PASSWORD:
            g_free (self->password);
            self->password = g_value_dup_string(value);
            break;
        case PROP_SERVER:
            g_free (self->server);
            self->server = g_value_dup_string(value);
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

    if(self->account != NULL) {
        purple_account_destroy(self->account);
        self->account = NULL;
    }

    G_OBJECT_CLASS (haze_connection_parent_class)->dispose (object);
}

static void
haze_connection_finalize (GObject *object)
{
    HazeConnection *self = HAZE_CONNECTION (object);

    g_free (self->username);
    g_free (self->password);
    g_free (self->server);

    G_OBJECT_CLASS (haze_connection_parent_class)->finalize (object);
}

static void
haze_connection_class_init (HazeConnectionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    TpBaseConnectionClass *base_class = TP_BASE_CONNECTION_CLASS (klass);
    GParamSpec *param_spec;

    g_debug("Initializing (HazeConnectionClass *)%p", klass);

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
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NAME |
                                      G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_USERNAME, param_spec);

    param_spec = g_param_spec_string ("password", "Account password",
                                      "The protocol to which to connect.",
                                      NULL,
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NAME |
                                      G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_PASSWORD, param_spec);

    param_spec = g_param_spec_string ("server", "Hostname or IP of server",
                                      "The server used when establishing a connection.",
                                      NULL,
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NAME |
                                      G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_SERVER, param_spec);
}

static void
haze_connection_init (HazeConnection *self)
{
    g_debug("Initializing (HazeConnection *)%p", self);
}
