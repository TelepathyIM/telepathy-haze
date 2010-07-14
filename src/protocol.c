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

#include "connection.h"
#include "debug.h"

G_DEFINE_TYPE (HazeProtocol, haze_protocol, TP_TYPE_BASE_PROTOCOL)

struct _HazeProtocolPrivate {
    gchar *prpl_id;
    PurplePluginProtocolInfo *prpl_info;
    HazeParameterMapping *parameter_map;
    TpCMParamSpec *paramspecs;
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

GList *
haze_protocol_build_list (void)
{
  HazeProtocolInfo *i;
  GList *iter;
  GList *ret = NULL;

  for (iter = purple_plugins_get_protocols (); iter; iter = iter->next)
    {
      PurplePlugin *plugin = iter->data;
      PurplePluginInfo *p_info = plugin->info;
      PurplePluginProtocolInfo *prpl_info =
          PURPLE_PLUGIN_PROTOCOL_INFO (plugin);
      HazeProtocol *protocol;
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
          const gchar *tp_name;

          if (g_str_has_prefix (p_info->id, "prpl-"))
            {
              tp_name = (p_info->id + 5);
            }
          else
            {
              g_warning ("prpl '%s' has a dumb id; spank its author",
                  p_info->id);
              tp_name = p_info->id;
            }

          /* default behaviour for unknown protocols */
          protocol = g_object_new (HAZE_TYPE_PROTOCOL,
              "name", tp_name,
              "prpl-id", p_info->id,
              "prpl-info", prpl_info,
              NULL);
        }
      else
        {
          protocol = g_object_new (HAZE_TYPE_PROTOCOL,
              "name", info->tp_protocol_name,
              "prpl-id", p_info->id,
              "prpl-info", prpl_info,
              "parameter-map", info->parameter_map,
              NULL);
        }

      ret = g_list_prepend (ret, protocol);
    }

  return ret;
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
haze_protocol_lookup_param (
    HazeProtocol *self,
    const gchar *purple_name)
{
  const HazeParameterMapping *m;

  for (m = self->priv->parameter_map; m != NULL && m->purple_name != NULL; m++)
    if (!tp_strdiff (m->purple_name, purple_name))
      return m;

  return NULL;
}

/*
 * Adds a separate field for each PurpleAccountUserSplit
 */
static void
_translate_protocol_usersplits (HazeProtocol *self,
    GArray *paramspecs)
{
  GList *l = self->priv->prpl_info->user_splits;
  const guint count = g_list_length (l);
  guint i;

  /* first user split is covered by "account" */
  for (i = 1; i <= count; i++)
    {
      gchar *usersplit = g_strdup_printf ("usersplit%d", i);
      const HazeParameterMapping *m = haze_protocol_lookup_param (self,
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
                            HazeProtocol *self)
{
    const char *pref_name = purple_account_option_get_setting (option);
    PurplePrefType pref_type = purple_account_option_get_type (option);
    gchar *name = NULL;
    const HazeParameterMapping *m = haze_protocol_lookup_param (self, pref_name);

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
            if (g_str_equal (self->priv->prpl_id, "prpl-bonjour")
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
static const TpCMParamSpec *
haze_protocol_get_parameters (TpBaseProtocol *protocol)
{
    HazeProtocol *self = HAZE_PROTOCOL (protocol);
    const TpCMParamSpec account_spec =
        { "account", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
          TP_CONN_MGR_PARAM_FLAG_REQUIRED, NULL, 0, NULL, NULL,
          (gpointer) "account", NULL };
    TpCMParamSpec password_spec =
        { "password", DBUS_TYPE_STRING_AS_STRING, G_TYPE_STRING,
          TP_CONN_MGR_PARAM_FLAG_REQUIRED | TP_CONN_MGR_PARAM_FLAG_SECRET,
          NULL, 0, NULL, NULL,
          (gpointer) "password", NULL };
    GArray *paramspecs;
    GList *opts;

    if (self->priv->paramspecs != NULL)
      goto finally;

    paramspecs = g_array_new (TRUE, TRUE, sizeof (TpCMParamSpec));

    /* TODO: local-xmpp shouldn't have an account parameter */
    g_array_append_val (paramspecs, account_spec);

    /* Translate user splits for protocols that have a mapping */
    if (self->priv->prpl_info->user_splits &&
        haze_protocol_lookup_param (self, "usersplit1") != NULL)
      _translate_protocol_usersplits (self, paramspecs);

    /* Password parameter: */
    if (!(self->priv->prpl_info->options & OPT_PROTO_NO_PASSWORD))
    {
        if (self->priv->prpl_info->options & OPT_PROTO_PASSWORD_OPTIONAL)
            password_spec.flags &= ~TP_CONN_MGR_PARAM_FLAG_REQUIRED;
        g_array_append_val (paramspecs, password_spec);
    }

    for (opts = self->priv->prpl_info->protocol_options;
        opts != NULL;
        opts = opts->next)
    {
        PurpleAccountOption *option = (PurpleAccountOption *)opts->data;
        TpCMParamSpec paramspec =
            { NULL, NULL, 0, 0, NULL, 0, NULL, NULL, NULL, NULL};

        if (_translate_protocol_option (option, &paramspec, self))
            g_array_append_val (paramspecs, paramspec);
    }

    self->priv->paramspecs = (TpCMParamSpec *) g_array_free (paramspecs,
        FALSE);

finally:
    return self->priv->paramspecs;
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

static GHashTable *
haze_protocol_translate_parameters (HazeProtocol *self,
    GHashTable *asv)
{
  GHashTable *unused = g_hash_table_new (g_str_hash, g_str_equal);
  GHashTable *purple_params = g_hash_table_new_full (g_str_hash, g_str_equal,
      NULL, (GDestroyNotify) tp_g_value_slice_free);
  const TpCMParamSpec *pspecs = haze_protocol_get_parameters (
      (TpBaseProtocol *) self);
  const TpCMParamSpec *pspec;

  tp_g_hash_table_update (unused, asv, NULL, NULL);

  for (pspec = pspecs; pspec->name != NULL; pspec++)
    {
      gchar *prpl_param_name = (gchar *) pspec->setter_data;
      const GValue *value = tp_asv_lookup (asv, pspec->name);

      if (value == NULL)
        continue;

      DEBUG ("setting parameter %s (telepathy name %s)", prpl_param_name,
          pspec->name);

      g_hash_table_insert (purple_params, prpl_param_name,
          tp_g_value_slice_dup (value));
      g_hash_table_remove (unused, pspec->name);
    }

  /* telepathy-glib isn't meant to give us parameters we don't understand */
  g_assert (g_hash_table_size (unused) == 0);
  g_hash_table_unref (unused);

  return purple_params;
}

static TpBaseConnection *
haze_protocol_new_connection (TpBaseProtocol *base,
    GHashTable *asv,
    GError **error)
{
  HazeProtocol *self = HAZE_PROTOCOL (base);
  HazeConnection *conn;
  gchar *name;
  GHashTable *purple_params = haze_protocol_translate_parameters (self, asv);

  g_object_get (self,
      "name", &name,
      NULL);

  conn = g_object_new (HAZE_TYPE_CONNECTION,
      "protocol", name,
      "prpl-id", self->priv->prpl_id,
      "prpl-info", self->priv->prpl_info,
      "parameters", purple_params,
      NULL);

  g_free (name);
  g_hash_table_unref (purple_params);

  if (!haze_connection_create_account (conn, error))
    {
      g_object_unref (conn);
      return NULL;
    }

  return (TpBaseConnection *) conn;
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
  g_free (self->priv->paramspecs);

  if (finalize != NULL)
    finalize (object);
}

static gchar *
haze_protocol_normalize_contact (TpBaseProtocol *base,
    const gchar *contact,
    GError **error)
{
  /* FIXME: is it safe to pass a NULL account to prpl_info->account for all
   * prpls? If it is, we could do that to be more likely to normalize right */
  return g_strdup (purple_normalize (NULL, contact));
}

static gchar *
haze_protocol_identify_account (TpBaseProtocol *base,
    GHashTable *asv,
    GError **error)
{
  HazeProtocol *self = HAZE_PROTOCOL (base);
  GHashTable *purple_params = haze_protocol_translate_parameters (self, asv);
  gchar *ret;

  ret = haze_connection_get_username (purple_params, self->priv->prpl_info,
      FALSE);
  g_hash_table_unref (purple_params);
  return ret;
}

static GStrv
haze_protocol_get_interfaces (TpBaseProtocol *base)
{
  return g_new0 (gchar *, 1);
}

static void
haze_protocol_get_connection_details (TpBaseProtocol *base,
    GStrv *connection_interfaces,
    GPtrArray **requestable_channel_classes,
    gchar **icon_name,
    gchar **english_name,
    gchar **vcard_field)
{
  if (connection_interfaces != NULL)
    {
      *connection_interfaces = g_strdupv (
        (gchar **) haze_connection_get_implemented_interfaces ());
    }

  if (requestable_channel_classes != NULL)
    {
      *requestable_channel_classes = g_ptr_array_new ();

      haze_im_channel_factory_append_channel_classes (
          *requestable_channel_classes);
      haze_contact_list_append_channel_classes (
          *requestable_channel_classes);
#ifdef ENABLE_MEDIA
      haze_media_manager_append_channel_classes (
          *requestable_channel_classes);
#endif
    }

  /* stub implementations for now, clients have to be able to fall back */

  if (english_name != NULL)
    *english_name = g_strdup ("");

  if (icon_name != NULL)
    *icon_name = g_strdup ("");

  if (vcard_field != NULL)
    *vcard_field = g_strdup ("");
}

static void
haze_protocol_class_init (HazeProtocolClass *cls)
{
  GObjectClass *object_class = (GObjectClass *) cls;
  TpBaseProtocolClass *base_class = (TpBaseProtocolClass *) cls;
  GParamSpec *param_spec;

  base_class->get_parameters = haze_protocol_get_parameters;
  base_class->new_connection = haze_protocol_new_connection;
  base_class->normalize_contact = haze_protocol_normalize_contact;
  base_class->identify_account = haze_protocol_identify_account;
  base_class->get_interfaces = haze_protocol_get_interfaces;
  base_class->get_connection_details = haze_protocol_get_connection_details;

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
