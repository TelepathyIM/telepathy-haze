#include <glib.h>
#include <dbus/dbus-protocol.h>

#include <prpl.h>

#include "connection-manager.h"

G_DEFINE_TYPE(HazeConnectionManager,
    haze_connection_manager,
    TP_TYPE_BASE_CONNECTION_MANAGER)

typedef struct _HazeParams HazeParams;

struct _HazeParams {
    gchar *username;
    gchar *password;
    gchar *server;
};

static const TpCMParamSpec params[] = {
  { "account", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
    TP_CONN_MGR_PARAM_FLAG_REQUIRED, NULL,
    G_STRUCT_OFFSET(HazeParams, username), NULL, NULL },
  { "password", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
    TP_CONN_MGR_PARAM_FLAG_REQUIRED, NULL,
    G_STRUCT_OFFSET(HazeParams, password), NULL, NULL },
  { "server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL,
    G_STRUCT_OFFSET(HazeParams, server), NULL, NULL },
  { NULL, NULL, 0, 0, NULL, 0, NULL, NULL }
};

static void *
alloc_params (void)
{
  return g_new0 (HazeParams, 1);
}

static void
free_params (void *p)
{
    HazeParams *params = (HazeParams *)p;
    g_free (params->username);
    g_free (params->password);
    g_free (params->server);

    g_free (params);
}

static TpCMProtocolSpec *
get_protocols() {
    GList* iter;
    TpCMProtocolSpec *protocols, *protocol;
    guint n_protocols;

    iter = purple_plugins_get_protocols();
    n_protocols = g_list_length(iter);

    protocols = g_new0(TpCMProtocolSpec, n_protocols + 1);

    for (protocol = protocols; iter; iter = iter->next) {
        PurplePlugin *plugin = iter->data;
        PurplePluginInfo *info = plugin->info;
        if (info && info->id) {
            if(g_str_has_prefix(info->id, "prpl-")) {
                protocol->name = g_strdup(info->id + 5);
                protocol->parameters = params;
                protocol->params_new = alloc_params;
                protocol->params_free = free_params;
                protocol++;
            }
        }
    }
    return protocols;
}

HazeConnection *
haze_connection_manager_get_haze_connection (HazeConnectionManager *self,
                                             PurpleAccount *account)
{
    HazeConnection *hc;
    GList *l = self->connections;

    while (l != NULL) {
        hc = l->data;
        if(hc->account == account) {
            return hc;
        }
    }

    return NULL;
}

static void
connection_shutdown_finished_cb (TpBaseConnection *conn,
                                 gpointer data)
{
    HazeConnectionManager *self = HAZE_CONNECTION_MANAGER (data);

    self->connections = g_list_remove(self->connections, conn);
}

static TpBaseConnection *
_haze_connection_manager_new_connection (TpBaseConnectionManager *base,
                                         const gchar *proto,
                                         TpIntSet *params_present,
                                         void *parsed_params,
                                         GError **error)
{
    HazeConnectionManager *self = HAZE_CONNECTION_MANAGER(base);
    HazeParams *params = (HazeParams *)parsed_params;
    HazeConnection *conn = g_object_new (HAZE_TYPE_CONNECTION,
                                         "protocol",    proto,
                                         "username",    params->username,
                                         "password",    params->password,
                                         "server",      params->server,
                                         NULL);

    self->connections = g_list_prepend(self->connections, conn);
    g_signal_connect (conn, "shutdown-finished",
                      G_CALLBACK (connection_shutdown_finished_cb),
                      self);

    return (TpBaseConnection *) conn;
}

static void
haze_connection_manager_class_init (HazeConnectionManagerClass *klass)
{
    TpBaseConnectionManagerClass *base_class =
        (TpBaseConnectionManagerClass *)klass;

    base_class->new_connection = _haze_connection_manager_new_connection;
    base_class->cm_dbus_name = "haze";
    base_class->protocol_params = get_protocols();
}

static void
haze_connection_manager_init (HazeConnectionManager *self)
{
    g_debug("Initializing (HazeConnectionManager *)%p", self);
}

HazeConnectionManager *
haze_connection_manager_get (void) {
    static HazeConnectionManager *manager = NULL;
    if (G_UNLIKELY(manager == NULL)) {
        manager = g_object_new (HAZE_TYPE_CONNECTION_MANAGER, NULL);
    }
    g_assert (manager != NULL);
    return manager;
}
