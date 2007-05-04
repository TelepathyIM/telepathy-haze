#include <glib.h>
#include <dbus/dbus-protocol.h>

#include <prpl.h>

#include "psyduck-connection-manager.h"

G_DEFINE_TYPE(PsyduckConnectionManager,
    psyduck_connection_manager,
    TP_TYPE_BASE_CONNECTION_MANAGER)

static TpBaseConnection *
_psyduck_connection_manager_new_connection (TpBaseConnectionManager *self,
                                            const gchar *proto,
                                            TpIntSet *params_present,
                                            void *parsed_params,
                                            GError **error)
{
    return NULL;
}

typedef struct _PsyduckParams PsyduckParams;

struct _PsyduckParams {
    gchar *account;
    gchar *password;
    gchar *server;
};

static const TpCMParamSpec params[] = {
  { "account", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
    TP_CONN_MGR_PARAM_FLAG_REQUIRED, NULL,
    G_STRUCT_OFFSET(PsyduckParams, account) },
  { "password", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
    TP_CONN_MGR_PARAM_FLAG_REQUIRED, NULL,
    G_STRUCT_OFFSET(PsyduckParams, password) },
  { "server", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING, 0, NULL,
    G_STRUCT_OFFSET(PsyduckParams, server) },
  { NULL, NULL, 0, 0, NULL, 0 }
};

static void *
alloc_params (void)
{
  return g_new0 (PsyduckParams, 1);
}

static void
free_params (void *p)
{
    PsyduckParams *params = (PsyduckParams *)p;
    g_free (params->account);
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

static void
psyduck_connection_manager_class_init (PsyduckConnectionManagerClass *klass)
{
  TpBaseConnectionManagerClass *base_class =
    (TpBaseConnectionManagerClass *)klass;

  base_class->new_connection = _psyduck_connection_manager_new_connection;
  base_class->cm_dbus_name = "psyduck";
  base_class->protocol_params = get_protocols();
}

static void
psyduck_connection_manager_init (PsyduckConnectionManager *self)
{
}
