/*
 * connection-manager.c - HazeConnectionManager source
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

#include <glib.h>
#include <dbus/dbus-protocol.h>

#include <libpurple/prpl.h>
#include <libpurple/accountopt.h>

#include "connection-manager.h"
#include "debug.h"

G_DEFINE_TYPE(HazeConnectionManager,
    haze_connection_manager,
    TP_TYPE_BASE_CONNECTION_MANAGER)

typedef struct _HazeParams HazeParams;

struct _HazeParams {
    gchar *username;
    gchar *password;
    gchar *server;
};

/** These are protocols for which stripping off the "prpl-" prefix is not
 *  sufficient, or for which special munging has to be done.
 */
static HazeProtocolInfo known_protocol_info[] = {
    { "gadugadu",   "prpl-gg",          NULL },
    { "groupwise",  "prpl-novell",      NULL },
    { "irc",        "prpl-irc",         NULL },
    { "sametime",   "prpl-meanwhile",   NULL },
    { "local-xmpp", "prpl-bonjour",     NULL },
    { NULL,         NULL,               NULL }
};

static const TpCMParamSpec params[] = {
  { "account", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
    TP_CONN_MGR_PARAM_FLAG_REQUIRED, NULL,
    G_STRUCT_OFFSET(HazeParams, username), NULL, NULL },
  { "password", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
    /* FIXME: zephyr for instance doesn't need a password */
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

struct _protocol_info_foreach_data
{
    TpCMProtocolSpec *protocols;
    guint index;
};

static void
_protocol_info_foreach (gpointer key,
                        gpointer value,
                        gpointer user_data)
{
    HazeProtocolInfo *info = (HazeProtocolInfo *)value;
    struct _protocol_info_foreach_data *data =
        (struct _protocol_info_foreach_data *)user_data;
    TpCMProtocolSpec *protocol = &(data->protocols[data->index]);

    protocol->name = info->tp_protocol_name;
    protocol->parameters = params;
    protocol->params_new = alloc_params;
    protocol->params_free = free_params;

    (data->index)++;
}

static TpCMProtocolSpec *
get_protocols (HazeConnectionManagerClass *klass)
{
    struct _protocol_info_foreach_data foreach_data;
    TpCMProtocolSpec *protocols;
    guint n_protocols;

    n_protocols = g_hash_table_size (klass->protocol_info_table);
    foreach_data.protocols = protocols = (TpCMProtocolSpec *)
        g_slice_alloc0 (sizeof (TpCMProtocolSpec) * (n_protocols + 1));
    foreach_data.index = 0;

    g_hash_table_foreach (klass->protocol_info_table, _protocol_info_foreach,
        &foreach_data);

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
    HazeConnectionManager *cm = HAZE_CONNECTION_MANAGER(base);
    HazeConnectionManagerClass *klass = HAZE_CONNECTION_MANAGER_GET_CLASS (cm);
    HazeParams *params = (HazeParams *)parsed_params;
    HazeProtocolInfo *info =
        g_hash_table_lookup (klass->protocol_info_table, proto);
    HazeConnection *conn = g_object_new (HAZE_TYPE_CONNECTION,
                                         "protocol",        proto,
                                         "protocol-info",   info,
                                         "username",        params->username,
                                         "password",        params->password,
                                         "server",          params->server,
                                         NULL);

    cm->connections = g_list_prepend(cm->connections, conn);
    g_signal_connect (conn, "shutdown-finished",
                      G_CALLBACK (connection_shutdown_finished_cb),
                      cm);

    return (TpBaseConnection *) conn;
}

/** Frees the slice-allocated HazeProtocolInfo pointed to by @a data.  Useful
 *  as the value-destroying callback in a hash table.
 */
static void
_protocol_info_slice_free (gpointer data)
{
    g_slice_free (HazeProtocolInfo, data);
}

/** Predicate for g_hash_table_find to search on prpl_id.
 *  @param key      (const gchar *)tp_protocol_name
 *  @param value    (HazeProtocolInfo *)info
 *  @param data     (const gchar *)prpl_id
 *  @return @c TRUE iff info->prpl_id eq prpl_id
 */
static gboolean
_compare_protocol_id (gpointer key,
                      gpointer value,
                      gpointer data)
{
    HazeProtocolInfo *info = (HazeProtocolInfo *)value;
    const gchar *prpl_id = (const gchar *)data;
    return (!strcmp (info->prpl_id, prpl_id));
}

static void
get_values_foreach (gpointer key,
                    gpointer value,
                    gpointer data)
{
    GList **values = data;

    *values = g_list_prepend (*values, value);
}

/* Equivalent to g_hash_table_get_values, which only exists in GLib >=2.14. */
static GList *
haze_g_hash_table_get_values (GHashTable *table)
{
    GList *values = NULL;

    g_hash_table_foreach (table, get_values_foreach, &values);

    return values;
}

static void _init_protocol_table (HazeConnectionManagerClass *klass)
{
    GHashTable *table;
    HazeProtocolInfo *i, *info;
    PurplePlugin *plugin;
    PurplePluginInfo *p_info;
    PurplePluginProtocolInfo *prpl_info;
    GList *iter;

    table = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
                                   _protocol_info_slice_free);

    for (i = known_protocol_info; i->prpl_id != NULL; i++)
    {
        plugin = purple_find_prpl (i->prpl_id);
        if (!plugin)
            continue;

        info = g_slice_new (HazeProtocolInfo);

        info->prpl_id = i->prpl_id;
        info->tp_protocol_name = i->tp_protocol_name;
        info->prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO (plugin);

        g_hash_table_insert (table, info->tp_protocol_name, info);
    }

    for (iter = purple_plugins_get_protocols (); iter; iter = iter->next)
    {
        plugin = (PurplePlugin *)iter->data;
        p_info = plugin->info;
        prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO (plugin);

        if (g_hash_table_find (table, _compare_protocol_id, p_info->id))
            continue; /* already in the table from the previous loop */

        info = g_slice_new (HazeProtocolInfo);
        info->prpl_id = p_info->id;
        if (g_str_has_prefix (p_info->id, "prpl-"))
            info->tp_protocol_name = (p_info->id + 5);
        else
        {
            g_warning ("prpl '%s' has a dumb id; spank its author", p_info->id);
            info->tp_protocol_name = p_info->id;
        }
        info->prpl_info = prpl_info;

        g_hash_table_insert (table, info->tp_protocol_name, info);
    }

    {
        GList *protocols = haze_g_hash_table_get_values (table);
        GList *l;
        GString *debug_string = g_string_new ("");

        for (l = protocols; l; l = l->next)
        {
            info = l->data;
            g_string_append (debug_string, info->tp_protocol_name);
            if (l->next)
                g_string_append (debug_string, ", ");
        }

        DEBUG ("Found protocols %s", debug_string->str);

        g_list_free (protocols);
        g_string_free (debug_string, TRUE);
    }

    klass->protocol_info_table = table;
}

static void
haze_connection_manager_class_init (HazeConnectionManagerClass *klass)
{
    TpBaseConnectionManagerClass *base_class =
        (TpBaseConnectionManagerClass *)klass;

    _init_protocol_table (klass);

    base_class->new_connection = _haze_connection_manager_new_connection;
    base_class->cm_dbus_name = "haze";
    base_class->protocol_params = get_protocols (klass);
}

static void
haze_connection_manager_init (HazeConnectionManager *self)
{
    DEBUG ("Initializing (HazeConnectionManager *)%p", self);
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
