#include "connection.h"
#include <telepathy-glib/handle-repo-dynamic.h>

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

gboolean
start_connecting (TpBaseConnection *self,
                  GError **error)
{
    return FALSE;
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

    base_class->create_handle_repos = _haze_connection_create_handle_repos;
    base_class->create_channel_factories =
        _haze_connection_create_channel_factories;

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
