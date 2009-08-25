/*
 * connection-manager.c - HazeConnectionManager source
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

#include <glib.h>
#include <dbus/dbus-protocol.h>

#include <libpurple/prpl.h>
#include <libpurple/accountopt.h>

#include "connection-manager.h"
#include "debug.h"

G_DEFINE_TYPE(HazeConnectionManager,
    haze_connection_manager,
    TP_TYPE_BASE_CONNECTION_MANAGER)

/* For some protocols, removing the "prpl-" prefix from its name in libpurple
 * doesn't give the right name for Telepathy.  Other protocols need some
 * parameters renaming to match well-known names in the spec, or to have
 * hyphens rather than underscores for consistency.
 */
static HazeProtocolInfo known_protocol_info[] = {
    { "aim",        "prpl-aim",         NULL, "" },
    /* Seriously. */
    { "facebook",   "prpl-bigbrownchunx-facebookim", NULL, "" },
    { "gadugadu",   "prpl-gg",          NULL, "" },
    { "groupwise",  "prpl-novell",      NULL, "" },
    { "irc",        "prpl-irc",         NULL, "encoding:charset" },
    { "icq",        "prpl-icq",         NULL, "encoding:charset" },
    { "jabber",     "prpl-jabber",      NULL,
        "connect_server:server,require_tls:require-encryption" },
    { "local-xmpp", "prpl-bonjour",     NULL,
        "first:first-name,last:last-name" },
    { "msn",        "prpl-msn",         NULL, "" },
    { "qq",         "prpl-qq",          NULL, "" },
    { "sametime",   "prpl-meanwhile",   NULL, "" },
    { "yahoo",      "prpl-yahoo",       NULL, "local_charset:charset" },
    { "yahoojp",    "prpl-yahoojp",     NULL, "local_charset:charset" },
    { "zephyr",     "prpl-zephyr",      NULL, "encoding:charset" },
    { NULL,         NULL,               NULL, "" }
};

static void *
_haze_cm_alloc_params (void)
{
    /* (gchar *) => (GValue *) */
    return g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
        (GDestroyNotify) tp_g_value_slice_free);
}

static void
_haze_cm_free_params (void *p)
{
    GHashTable *params = (GHashTable *)p;
    g_hash_table_unref (params);
}

static void
_haze_cm_set_param (const TpCMParamSpec *paramspec,
                    const GValue *value,
                    gpointer params_)
{
    GHashTable *params = (GHashTable *) params_;
    GValue *value_copy = tp_g_value_slice_new (paramspec->gtype);
    gchar *prpl_param_name = (gchar *) paramspec->setter_data;

    g_assert (G_VALUE_TYPE (value) == G_VALUE_TYPE (value_copy));

    g_value_copy (value, value_copy);

    DEBUG ("setting parameter %s (telepathy name %s)",
        prpl_param_name, paramspec->name);

    g_hash_table_insert (params, prpl_param_name, value_copy);
}

static gboolean
_param_filter_no_blanks (const TpCMParamSpec *paramspec,
                         GValue *value,
                         GError **error)
{
    const gchar *str = g_value_get_string (value);

    if (*str == '\0')
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
            "Account parameter '%s' must not be empty",
            paramspec->name);
        return FALSE;
    }

    if (strstr (str, " ") != NULL)
    {
        g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
            "Account parameter '%s' may not contain spaces",
            paramspec->name);
        return FALSE;
    }

    return TRUE;
}

/* Checks whether the supplied string equals one of those in the GList
 * paramspec->filter_data.
 */
static gboolean
_param_filter_string_list (const TpCMParamSpec *paramspec,
                           GValue *value,
                           GError **error)
{
    const gchar *str = g_value_get_string (value);
    const GList *valid_values = paramspec->filter_data;

    for (; valid_values != NULL; valid_values = valid_values->next)
    {
        const gchar *valid = valid_values->data;

        if (!tp_strdiff (valid, str))
            return TRUE;
    }

    g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
        "'%s' is not a valid value for parameter '%s'", str, paramspec->name);
    return FALSE;
}

/* Populates a TpCMParamSpec from a PurpleAccountOption, possibly renaming the
 * parameter as specified in parameter_map.  paramspec is assumed to be zeroed out.
 * Returns TRUE on success, and FALSE if paramspec could not be populated (and
 * thus should not be used).
 */
static gboolean
_translate_protocol_option (PurpleAccountOption *option,
                            TpCMParamSpec *paramspec,
                            HazeProtocolInfo *hpi,
                            GHashTable *parameter_map)
{
    const char *pref_name = purple_account_option_get_setting (option);
    PurplePrefType pref_type = purple_account_option_get_type (option);
    gchar *name = g_strdup (g_hash_table_lookup (parameter_map, pref_name));

    /* These strings are never free'd, but need to last until exit anyway.
     */
    if (name == NULL)
      name = g_strdup (pref_name);

    if (g_str_has_prefix (name, "facebook_"))
      {
        gchar *tmp = g_strdup (name + strlen ("facebook_"));
        g_free (name);
        name = tmp;
      }

    g_strdelimit (name, "_", '-');
    paramspec->name = name;

    paramspec->setter_data = option->pref_name;
    /* TODO: does libpurple ever require a parameter besides the username
     *       and possibly password?
     */
    paramspec->flags = 0;

    switch (pref_type)
    {
        case PURPLE_PREF_BOOLEAN:
            paramspec->dtype = DBUS_TYPE_BOOLEAN_AS_STRING;
            paramspec->gtype = G_TYPE_BOOLEAN;
            paramspec->flags |= TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT;
            paramspec->def = GINT_TO_POINTER (
                purple_account_option_get_default_bool (option));
            break;
        case PURPLE_PREF_INT:
            paramspec->dtype = DBUS_TYPE_INT32_AS_STRING;
            paramspec->gtype = G_TYPE_INT;
            paramspec->flags |= TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT;
            paramspec->def = GINT_TO_POINTER (
                purple_account_option_get_default_int (option));
            break;
        case PURPLE_PREF_STRING:
        {
            const gchar *def;

            paramspec->dtype = DBUS_TYPE_STRING_AS_STRING;
            paramspec->gtype = G_TYPE_STRING;

            /* prpl-bonjour chooses the defaults for these parameters with
             * getpwuid(3); but for haze's purposes that's the UI's job.
             */
            if (g_str_equal (hpi->prpl_id, "prpl-bonjour")
                && (g_str_equal (paramspec->name, "first-name")
                    || g_str_equal (paramspec->name, "last-name")))
            {
                paramspec->flags |= TP_CONN_MGR_PARAM_FLAG_REQUIRED;
                break;
            }

            if (g_str_equal (paramspec->name, "charset"))
                def = "UTF-8";
            else
                def = purple_account_option_get_default_string (option);

            if (def != NULL && *def != '\0')
            {
                paramspec->def = def;
                paramspec->flags |= TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT;
            }
            break;
        }
        case PURPLE_PREF_STRING_LIST:
        {
            const gchar *def;
            const GList *option_tuples;
            GList *valid_strings = NULL;
            /* tuple->key is human-readable description, tuple->value is the
             * value's ID and is secretly a (const char *).
             */
            const PurpleKeyValuePair *tuple;

            paramspec->dtype = DBUS_TYPE_STRING_AS_STRING;
            paramspec->gtype = G_TYPE_STRING;

            option_tuples = purple_account_option_get_list (option);
            for (; option_tuples != NULL; option_tuples = option_tuples->next)
            {
                tuple = option_tuples->data;
                valid_strings = g_list_prepend (valid_strings, tuple->value);
            }
            paramspec->filter = _param_filter_string_list;
            paramspec->filter_data = valid_strings;

            def = purple_account_option_get_default_list_value (option);
            if (def != NULL && *def != '\0')
            {
                paramspec->def = def;
                paramspec->flags |= TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT;
            }
            break;
        }
        default:
            g_warning ("account option %s has unknown type %u; ignoring",
                pref_name, pref_type);
            return FALSE;
    }

    if (g_str_equal (paramspec->name, "server"))
        paramspec->filter = _param_filter_no_blanks;

    return TRUE;
}

/* Constructs a parameter specification from the prpl's options list, renaming
 * protocols and parameters according to known_protocol_info.
 */
static TpCMParamSpec *
_build_paramspecs (HazeProtocolInfo *hpi)
{
    const TpCMParamSpec account_spec =
        { "account", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
          TP_CONN_MGR_PARAM_FLAG_REQUIRED, NULL, 0, NULL, NULL,
          (gpointer) "account", NULL };
    TpCMParamSpec password_spec =
        { "password", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
          TP_CONN_MGR_PARAM_FLAG_REQUIRED, NULL, 0, NULL, NULL,
          (gpointer) "password", NULL };

    GArray *paramspecs = g_array_new (TRUE, TRUE, sizeof (TpCMParamSpec));
    GList *opts;

    /* Deserialize the hpi->parameter_map string to a hash table;
     *     "libpurple_name1:telepathy-name1,libpurple_name2:telepathy-name2"
     * becomes
     *     "libpurple_name1" => "telepathy-name1"
     *     "libpurple_name2" => "telepathy-name2"
     */
    GHashTable *parameter_map = g_hash_table_new (g_str_hash, g_str_equal);
    gchar **map_chunks = g_strsplit_set (hpi->parameter_map, ",:", 0);
    int i;
    for (i = 0; map_chunks[i] != NULL; i = i + 2)
    {
        g_assert (map_chunks[i+1] != NULL);
        g_hash_table_insert (parameter_map, map_chunks[i], map_chunks[i+1]);
    }

    /* TODO: local-xmpp shouldn't have an account parameter */
    g_array_append_val (paramspecs, account_spec);

    /* Password parameter: */
    if (!(hpi->prpl_info->options & OPT_PROTO_NO_PASSWORD))
    {
        if (hpi->prpl_info->options & OPT_PROTO_PASSWORD_OPTIONAL)
            password_spec.flags = 0;
        g_array_append_val (paramspecs, password_spec);
    }

    for (opts = hpi->prpl_info->protocol_options; opts; opts = opts->next)
    {
        PurpleAccountOption *option = (PurpleAccountOption *)opts->data;
        TpCMParamSpec paramspec =
            { NULL, NULL, 0, 0, NULL, 0, NULL, NULL, NULL, NULL};

        if (_translate_protocol_option (option, &paramspec, hpi, parameter_map))
            g_array_append_val (paramspecs, paramspec);
    }

    g_hash_table_destroy (parameter_map);
    g_strfreev (map_chunks);

    return (TpCMParamSpec *) g_array_free (paramspecs, FALSE);
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
    protocol->parameters = _build_paramspecs (info);
    protocol->params_new = _haze_cm_alloc_params;
    protocol->params_free = _haze_cm_free_params;
    protocol->set_param = _haze_cm_set_param;

    (data->index)++;
}

static int
_compare_protocol_names(gconstpointer a,
                        gconstpointer b)
{
    const TpCMProtocolSpec *protocol_a = a;
    const TpCMProtocolSpec *protocol_b = b;

    return strcmp(protocol_a->name, protocol_b->name);
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

    qsort (protocols, n_protocols, sizeof (TpCMProtocolSpec),
        _compare_protocol_names);

    {
        GString *debug_string = g_string_new ("");
        TpCMProtocolSpec *p = protocols;
        while (p->name != NULL)
        {
            g_string_append (debug_string, p->name);
            p += 1;
            if (p->name != NULL)
                g_string_append (debug_string, ", ");
        }

        DEBUG ("Found protocols %s", debug_string->str);
        g_string_free (debug_string, TRUE);
    }

    return protocols;
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
    GHashTable *params = (GHashTable *)parsed_params;
    HazeProtocolInfo *info =
        g_hash_table_lookup (klass->protocol_info_table, proto);
    HazeConnection *conn = g_object_new (HAZE_TYPE_CONNECTION,
                                         "protocol",        proto,
                                         "protocol-info",   info,
                                         "parameters",      params,
                                         NULL);

    if (!haze_connection_create_account (conn, error))
      {
        g_object_unref (conn);
        return FALSE;
      }
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
        info->parameter_map = i->parameter_map;

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
        info->parameter_map = "";

        g_hash_table_insert (table, info->tp_protocol_name, info);
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
