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

#include <telepathy-glib/telepathy-glib.h>

G_DEFINE_TYPE (HazeProtocol, haze_protocol, TP_TYPE_BASE_PROTOCOL)

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

GHashTable *
haze_protocol_build_protocol_table (void)
{
  GHashTable *table;
  HazeProtocolInfo *i;
  GList *iter;

  table = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, NULL);

  for (i = known_protocol_info; i->prpl_id != NULL; i++)
    {
      PurplePlugin *plugin = purple_find_prpl (i->prpl_id);

      if (plugin == NULL)
        continue;

      i->prpl_info = PURPLE_PLUGIN_PROTOCOL_INFO (plugin);

      g_hash_table_insert (table, i->tp_protocol_name, i);
    }

  for (iter = purple_plugins_get_protocols (); iter; iter = iter->next)
    {
      PurplePlugin *plugin = iter->data;
      PurplePluginInfo *p_info = plugin->info;
      PurplePluginProtocolInfo *prpl_info =
          PURPLE_PLUGIN_PROTOCOL_INFO (plugin);
      HazeProtocolInfo *info;

      if (g_hash_table_find (table, _compare_protocol_id, p_info->id))
        continue; /* already in the table from the previous loop */

      info = g_slice_new (HazeProtocolInfo);
      info->prpl_id = p_info->id;
      info->prpl_info = prpl_info;
      info->parameter_map = NULL;

      if (g_str_has_prefix (p_info->id, "prpl-"))
        {
          info->tp_protocol_name = (p_info->id + 5);
        }
      else
        {
          g_warning ("prpl '%s' has a dumb id; spank its author", p_info->id);
          info->tp_protocol_name = p_info->id;
        }

      g_hash_table_insert (table, info->tp_protocol_name, info);
    }

  return table;
}

static void
haze_protocol_init (HazeProtocol *self)
{
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
haze_protocol_class_init (HazeProtocolClass *cls)
{
  TpBaseProtocolClass *base_class = (TpBaseProtocolClass *) cls;

  base_class->get_parameters = haze_protocol_get_parameters;
  base_class->new_connection = haze_protocol_new_connection;
}
