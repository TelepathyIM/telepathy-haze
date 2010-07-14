/*
 * Haze Protocol object
 *
 * Copyright © 2007 Will Thompson
 * Copyright © 2007-2010 Collabora Ltd.
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

#include "config.h"
#include "protocol.h"

#include <string.h>

#include <dbus/dbus-protocol.h>
#include <libpurple/accountopt.h>
#include <libpurple/prpl.h>
#include <telepathy-glib/telepathy-glib.h>

G_DEFINE_TYPE (HazeProtocol, haze_protocol, TP_TYPE_BASE_PROTOCOL)

struct _HazeProtocolPrivate {
    gchar *prpl_id;
    PurplePluginProtocolInfo *prpl_info;
    HazeParameterMapping *parameter_map;
};

/* For some protocols, removing the "prpl-" prefix from its name in libpurple
 * doesn't give the right name for Telepathy.  Other protocols need some
 * parameters renaming to match well-known names in the spec, or to have
 * hyphens rather than underscores for consistency.
 */

static const HazeParameterMapping encoding_to_charset[] = {
    { "encoding", "charset" },
    { NULL, NULL }
};

static const HazeParameterMapping jabber_mappings[] = {
    { "connect_server", "server" },
    { "require_tls", "require-encryption" },
    { NULL, NULL }
};

static const HazeParameterMapping bonjour_mappings[] = {
    { "first", "first-name" },
    { "last", "last-name" },
    { NULL, NULL }
};

static const HazeParameterMapping sipe_mappings[] = {
    { "usersplit1", "login" },
    { NULL, NULL }
};

static const HazeParameterMapping yahoo_mappings[] = {
    { "local_charset", "charset" },
    { NULL, NULL }
};

static HazeProtocolInfo known_protocol_info[] = {
    { "aim", "prpl-aim", NULL, NULL },
    /* Seriously. */
    { "facebook", "prpl-bigbrownchunx-facebookim", NULL, NULL },
    { "gadugadu", "prpl-gg", NULL, NULL },
    { "groupwise", "prpl-novell", NULL, NULL },
    { "irc", "prpl-irc", NULL, encoding_to_charset },
    { "icq", "prpl-icq", NULL, encoding_to_charset },
    { "jabber", "prpl-jabber", NULL, jabber_mappings },
    { "local-xmpp", "prpl-bonjour", NULL, bonjour_mappings },
    { "msn", "prpl-msn", NULL, NULL },
    { "qq", "prpl-qq", NULL, NULL },
    { "sametime", "prpl-meanwhile", NULL, NULL },
    { "sipe", "prpl-sipe", NULL, sipe_mappings },
    { "yahoo", "prpl-yahoo", NULL, yahoo_mappings },
    { "yahoojp", "prpl-yahoojp", NULL, yahoo_mappings },
    { "zephyr", "prpl-zephyr", NULL, encoding_to_charset },
    { "mxit", "prpl-loubserp-mxit", NULL, NULL },
    { "sip", "prpl-simple", NULL, NULL },
    { NULL, NULL, NULL, NULL }
};

GHashTable *
haze_protocol_build_protocol_table (void)
{
  static GHashTable *table = NULL;
  HazeProtocolInfo *i;
  GList *iter;

  if (table != NULL)
    return table;

  table = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);

  for (iter = purple_plugins_get_protocols (); iter; iter = iter->next)
    {
      PurplePlugin *plugin = iter->data;
      PurplePluginInfo *p_info = plugin->info;
      PurplePluginProtocolInfo *prpl_info =
          PURPLE_PLUGIN_PROTOCOL_INFO (plugin);
      HazeProtocolInfo *info = NULL;

      for (i = known_protocol_info; i->prpl_id != NULL; i++)
        {
          if (!tp_strdiff (i->prpl_id, p_info->id))
            {
              info = i;
              break;
            }
        }

      if (info == NULL)
        {
          /* one intentional leak per unknown protocol per process */
          info = g_slice_new (HazeProtocolInfo);
          info->prpl_id = p_info->id;
          info->parameter_map = NULL;

          if (g_str_has_prefix (p_info->id, "prpl-"))
            {
              info->tp_protocol_name = (p_info->id + 5);
            }
          else
            {
              g_warning ("prpl '%s' has a dumb id; spank its author",
                  p_info->id);
              info->tp_protocol_name = p_info->id;
            }
        }

      info->prpl_info = prpl_info;
      g_hash_table_insert (table, info->tp_protocol_name, info);
    }

  return table;
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
TpCMParamSpec *
haze_protocol_info_to_param_specs (HazeProtocolInfo *hpi)
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

enum
{
  PROP_PRPL_ID = 1,
  PROP_PRPL_INFO,
  PROP_PARAMETER_MAP,
} HazeProtocolProperties;

static void
haze_protocol_init (HazeProtocol *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, HAZE_TYPE_PROTOCOL,
      HazeProtocolPrivate);
}

static const TpCMParamSpec *
haze_protocol_get_parameters (TpBaseProtocol *self)
{
  g_assert_not_reached ();
}

static TpBaseConnection *
haze_protocol_new_connection (TpBaseProtocol *self,
    GHashTable *asv,
    GError **error)
{
  g_assert_not_reached ();
}

static void
haze_protocol_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  HazeProtocol *self = HAZE_PROTOCOL (object);

  switch (property_id)
    {
    case PROP_PARAMETER_MAP:
      g_value_set_pointer (value, self->priv->parameter_map);
      break;

    case PROP_PRPL_ID:
      g_value_set_string (value, self->priv->prpl_id);
      break;

    case PROP_PRPL_INFO:
      g_value_set_pointer (value, self->priv->prpl_info);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
haze_protocol_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  HazeProtocol *self = HAZE_PROTOCOL (object);

  switch (property_id)
    {
    case PROP_PARAMETER_MAP:
      g_assert (self->priv->parameter_map == NULL); /* construct-only */
      self->priv->parameter_map = g_value_get_pointer (value);
      break;

    case PROP_PRPL_ID:
      g_assert (self->priv->prpl_id == NULL); /* construct-only */
      self->priv->prpl_id = g_value_dup_string (value);
      break;

    case PROP_PRPL_INFO:
      g_assert (self->priv->prpl_info == NULL); /* construct-only */
      self->priv->prpl_info = g_value_get_pointer (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
haze_protocol_finalize (GObject *object)
{
  GObjectFinalizeFunc finalize =
    G_OBJECT_CLASS (haze_protocol_parent_class)->finalize;
  HazeProtocol *self = HAZE_PROTOCOL (object);

  g_free (self->priv->prpl_id);

  if (finalize != NULL)
    finalize (object);
}

static void
haze_protocol_class_init (HazeProtocolClass *cls)
{
  GObjectClass *object_class = (GObjectClass *) cls;
  TpBaseProtocolClass *base_class = (TpBaseProtocolClass *) cls;
  GParamSpec *param_spec;

  base_class->get_parameters = haze_protocol_get_parameters;
  base_class->new_connection = haze_protocol_new_connection;

  g_type_class_add_private (cls, sizeof (HazeProtocolPrivate));
  object_class->get_property = haze_protocol_get_property;
  object_class->set_property = haze_protocol_set_property;
  object_class->finalize = haze_protocol_finalize;

  param_spec = g_param_spec_string ("prpl-id", "protocol plugin ID",
      "protocol plugin ID", NULL,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PRPL_ID, param_spec);

  param_spec = g_param_spec_pointer ("prpl-info", "PurplePluginProtocolInfo",
      "protocol plugin info",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PRPL_INFO, param_spec);

  param_spec = g_param_spec_pointer ("parameter-map", "HazeParameterMap",
      "protocol parameter map",
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_PARAMETER_MAP,
      param_spec);
}
