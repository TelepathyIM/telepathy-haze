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

#include <telepathy-glib/debug-sender.h>

#include "connection-manager.h"
#include "debug.h"

G_DEFINE_TYPE(HazeConnectionManager,
    haze_connection_manager,
    TP_TYPE_BASE_CONNECTION_MANAGER)

typedef struct _HazeConnectionManagerPrivate HazeConnectionManagerPrivate;
struct _HazeConnectionManagerPrivate
{
    TpDebugSender *debug_sender;
};

static void *
_haze_cm_alloc_params (void)
{
    /* (gchar *) => (GValue *) */
    return g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
        (GDestroyNotify) tp_g_value_slice_free);
}

static void
_haze_cm_set_param (const TpCMParamSpec *paramspec,
                    const GValue *value,
                    gpointer params_)
{
  GHashTable *params = params_;
  gchar *prpl_param_name = (gchar *) paramspec->setter_data;

  DEBUG ("setting parameter %s (telepathy name %s)",
      prpl_param_name, paramspec->name);

  g_hash_table_insert (params, prpl_param_name, tp_g_value_slice_dup (value));
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
  /* grr g_list_find_custom() is not const-correct of course */
  GList *valid_values = (GList *) paramspec->filter_data;

  if (g_list_find_custom (valid_values, str, (GCompareFunc) g_strcmp0)
      != NULL)
    return TRUE;

  g_set_error (error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
      "'%s' is not a valid value for parameter '%s'", str, paramspec->name);
  return FALSE;
}

static const HazeParameterMapping *
protocol_info_lookup_param (
    HazeProtocolInfo *hpi,
    const gchar *purple_name)
{
  const HazeParameterMapping *m;

  for (m = hpi->parameter_map; m != NULL && m->purple_name != NULL; m++)
    if (!tp_strdiff (m->purple_name, purple_name))
      return m;

  return NULL;
}

/*
 * Adds a separate field for each PurpleAccountUserSplit
 */
static void
_translate_protocol_usersplits (HazeProtocolInfo *hpi,
    GArray *paramspecs)
{
  GList *l = hpi->prpl_info->user_splits;
  const guint count = g_list_length (l);
  guint i;

  /* first user split is covered by "account" */
  for (i = 1; i <= count; i++)
    {
      gchar *usersplit = g_strdup_printf ("usersplit%d", i);
      const HazeParameterMapping *m = protocol_info_lookup_param (hpi,
          usersplit);
      gchar *name = NULL;
      TpCMParamSpec usersplit_spec = {
          NULL, /* name */
          DBUS_TYPE_STRING_AS_STRING,
          G_TYPE_STRING,
          0,
          NULL, 0, NULL, NULL,
          NULL, /* setter_data */
          NULL
      };

      if (m != NULL)
        name = g_strdup (m->telepathy_name);

      if (name == NULL)
        name = usersplit;

      usersplit_spec.name = name;
      usersplit_spec.setter_data = usersplit;

      g_array_append_val (paramspecs, usersplit_spec);
    }
}

/* Populates a TpCMParamSpec from a PurpleAccountOption, possibly renaming the
 * parameter as specified in hpi->parameter_map.  paramspec is assumed to be
 * zeroed out.
 *
 * Returns: %TRUE on success, and %FALSE if paramspec could not be populated
 *          (and thus should not be used).
 */
static gboolean
_translate_protocol_option (PurpleAccountOption *option,
                            TpCMParamSpec *paramspec,
                            HazeProtocolInfo *hpi)
{
    const char *pref_name = purple_account_option_get_setting (option);
    PurplePrefType pref_type = purple_account_option_get_type (option);
    gchar *name = NULL;
    const HazeParameterMapping *m = protocol_info_lookup_param (hpi, pref_name);

    /* Intentional once-per-protocol-per-process leak. */
    if (m != NULL)
      name = g_strdup (m->telepathy_name);
    else
      name = g_strdup (pref_name);

    if (g_str_has_prefix (name, "facebook_"))
      name += strlen ("facebook_");

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
            /* The spec decrees that ports should be uint16, and people get
             * very upset if they're not.  I suppose technically there could be
             * int parameters whose names end in "port" which aren't meant to
             * be unsigned?
             */
            if (g_str_has_suffix (name, "port"))
              {
                paramspec->dtype = DBUS_TYPE_UINT16_AS_STRING;
                paramspec->gtype = G_TYPE_UINT;
              }
            else
              {
                paramspec->dtype = DBUS_TYPE_INT32_AS_STRING;
                paramspec->gtype = G_TYPE_INT;
              }

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

    /* There don't seem to be any secrets except for password at the moment
     * (SILC's private-key is a filename, so its value is not actually secret).
     * If more appear, e.g. http-proxy-password, this would be a good place to
     * set the SECRET flag on them; for future-proofing I'll assume tha
     * anything ending with -password is likely to be secret. */
    if (g_str_has_suffix (paramspec->name, "-password"))
        paramspec->flags |= TP_CONN_MGR_PARAM_FLAG_SECRET;

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
          TP_CONN_MGR_PARAM_FLAG_REQUIRED | TP_CONN_MGR_PARAM_FLAG_SECRET,
          NULL, 0, NULL, NULL,
          (gpointer) "password", NULL };

    GArray *paramspecs = g_array_new (TRUE, TRUE, sizeof (TpCMParamSpec));
    GList *opts;

    /* TODO: local-xmpp shouldn't have an account parameter */
    g_array_append_val (paramspecs, account_spec);

    /* Translate user splits for protocols that have a mapping */
    if (hpi->prpl_info->user_splits &&
        protocol_info_lookup_param (hpi, "usersplit1") != NULL)
      _translate_protocol_usersplits (hpi, paramspecs);

    /* Password parameter: */
    if (!(hpi->prpl_info->options & OPT_PROTO_NO_PASSWORD))
    {
        if (hpi->prpl_info->options & OPT_PROTO_PASSWORD_OPTIONAL)
            password_spec.flags &= ~TP_CONN_MGR_PARAM_FLAG_REQUIRED;
        g_array_append_val (paramspecs, password_spec);
    }

    for (opts = hpi->prpl_info->protocol_options; opts; opts = opts->next)
    {
        PurpleAccountOption *option = (PurpleAccountOption *)opts->data;
        TpCMParamSpec paramspec =
            { NULL, NULL, 0, 0, NULL, 0, NULL, NULL, NULL, NULL};

        if (_translate_protocol_option (option, &paramspec, hpi))
            g_array_append_val (paramspecs, paramspec);
    }

    return (TpCMParamSpec *) g_array_free (paramspecs, FALSE);
}

static int
_compare_protocol_names (gconstpointer a,
                         gconstpointer b)
{
  const TpCMProtocolSpec *protocol_a = a;
  const TpCMProtocolSpec *protocol_b = b;

  return strcmp(protocol_a->name, protocol_b->name);
}

static TpCMProtocolSpec *
get_protocols (HazeConnectionManagerClass *klass)
{
  GArray *protocols = g_array_new (TRUE, TRUE, sizeof (TpCMProtocolSpec));
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, klass->protocol_info_table);
  while (g_hash_table_iter_next (&iter, &key, &value))
    {
      HazeProtocolInfo *info = value;
      TpCMProtocolSpec protocol = {
          info->tp_protocol_name, /* name */
          _build_paramspecs (info), /* parameters */
          _haze_cm_alloc_params, /* params_new */
          (GDestroyNotify) g_hash_table_unref, /* params_free */
          _haze_cm_set_param /* set_param */
      };

      g_array_append_val (protocols, protocol);
    }

  qsort (protocols->data, protocols->len, sizeof (TpCMProtocolSpec),
      _compare_protocol_names);

  {
    GString *debug_string = g_string_new ("");
    TpCMProtocolSpec *p = (TpCMProtocolSpec *) protocols->data;

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

  return (TpCMProtocolSpec *) g_array_free (protocols, FALSE);
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
        "protocol", proto,
        "prpl-id", info->prpl_id,
        "prpl-info", info->prpl_info,
        "parameters", params,
        NULL);

    if (!haze_connection_create_account (conn, error))
      {
        g_object_unref (conn);
        return FALSE;
      }
    return (TpBaseConnection *) conn;
}

static void
_haze_cm_finalize (GObject *object)
{
    HazeConnectionManager *self = HAZE_CONNECTION_MANAGER (object);
    void (*chain_up) (GObject *) =
      G_OBJECT_CLASS (haze_connection_manager_parent_class)->finalize;
    HazeConnectionManagerPrivate *priv = self->priv;

    if (priv->debug_sender != NULL)
    {
        g_object_unref (priv->debug_sender);
        priv->debug_sender = NULL;
    }

    if (chain_up != NULL)
    {
        chain_up (object);
    }
}

static void
haze_connection_manager_class_init (HazeConnectionManagerClass *klass)
{
    TpBaseConnectionManagerClass *base_class =
        (TpBaseConnectionManagerClass *)klass;
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    klass->protocol_info_table = haze_protocol_build_protocol_table ();

    object_class->finalize = _haze_cm_finalize;

    base_class->new_connection = _haze_connection_manager_new_connection;
    base_class->cm_dbus_name = "haze";
    base_class->protocol_params = get_protocols (klass);

    g_type_class_add_private (klass, sizeof (HazeConnectionManagerPrivate));
}

static void
haze_connection_manager_init (HazeConnectionManager *self)
{
    HazeConnectionManagerPrivate *priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
        HAZE_TYPE_CONNECTION_MANAGER, HazeConnectionManagerPrivate);

    self->priv = priv;

    priv->debug_sender = tp_debug_sender_dup ();
    g_log_set_default_handler (tp_debug_sender_log_handler, G_LOG_DOMAIN);

    DEBUG ("Initializing (HazeConnectionManager *)%p", self);
}
