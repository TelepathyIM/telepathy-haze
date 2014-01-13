/*
 * connection-aliasing.c - Aliasing interface implementation of HazeConnection
 * Copyright (C) 2007 Will Thompson
 * Copyright (C) 2007-2008 Collabora Ltd.
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

#include <config.h>
#include "connection-aliasing.h"

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include "connection.h"
#include "debug.h"

static gboolean
can_alias (HazeConnection *conn)
{
    PurplePluginProtocolInfo *prpl;

    g_assert (!purple_account_is_disconnected (conn->account));

    prpl = HAZE_CONNECTION_GET_PRPL_INFO (conn);

    return (prpl->alias_buddy != NULL);
}

typedef enum {
    DP_FLAGS
} AliasingDBusProperty;

static TpDBusPropertiesMixinPropImpl props[] = {
      { "AliasFlags", GINT_TO_POINTER (DP_FLAGS), NULL },
      { NULL }
};
TpDBusPropertiesMixinPropImpl *haze_connection_aliasing_properties = props;

void
haze_connection_aliasing_properties_getter (GObject *object,
    GQuark interface,
    GQuark name,
    GValue *value,
    gpointer getter_data)
{
  AliasingDBusProperty which = GPOINTER_TO_INT (getter_data);
  HazeConnection *conn = HAZE_CONNECTION (object);
  TpBaseConnection *base = (TpBaseConnection *) conn;

  if (tp_base_connection_get_status (base) != TP_CONNECTION_STATUS_CONNECTED)
    {
      /* not CONNECTED yet, so our connection doesn't have the prpl info
       * yet - return dummy values */
      if (G_VALUE_HOLDS_UINT (value))
        g_value_set_uint (value, 0);
      else
        g_assert_not_reached ();

      return;
    }

  switch (which)
    {
      case DP_FLAGS:
          {
            guint flags = 0;

            if (can_alias (conn))
              {
                flags = TP_CONNECTION_ALIAS_FLAG_USER_SET;
              }

            g_value_set_uint (value, flags);
          }
        break;

      default:
        g_assert_not_reached ();
    }
}

static const gchar *
get_alias (HazeConnection *self,
           TpHandle handle)
{
    TpBaseConnection *base = TP_BASE_CONNECTION (self);
    TpHandleRepoIface *contact_handles =
        tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);
    const gchar *bname = tp_handle_inspect (contact_handles, handle);
    const gchar *alias;

    if (handle == tp_base_connection_get_self_handle (base))
    {
        alias = purple_connection_get_display_name (self->account->gc);

        if (alias == NULL)
        {
            DEBUG ("self (%s) has no display_name", bname);
            alias = bname;
        }
    }
    else
    {
        PurpleBuddy *buddy = purple_find_buddy (self->account, bname);

        if (buddy != NULL)
        {
            alias = purple_buddy_get_alias (buddy);
        }
        else
        {
            DEBUG ("%s not on blist", bname);
            alias = bname;
        }
    }
    DEBUG ("%s has alias \"%s\"", bname, alias);
    return alias;
}

static void
haze_connection_request_aliases (TpSvcConnectionInterfaceAliasing1 *self,
                                 const GArray *contacts,
                                 DBusGMethodInvocation *context)
{
    HazeConnection *conn = HAZE_CONNECTION (self);
    TpBaseConnection *base = TP_BASE_CONNECTION (conn);
    TpHandleRepoIface *contact_handles =
        tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);
    guint i;
    GError *error = NULL;
    const gchar **aliases;

    if (!tp_handles_are_valid (contact_handles, contacts, FALSE, &error))
    {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
        return;
    }

    aliases = g_new0 (const gchar *, contacts->len + 1);

    for (i = 0; i < contacts->len; i++)
    {
        TpHandle handle = g_array_index (contacts, TpHandle, i);

        aliases[i] = get_alias (conn, handle);
    }

    tp_svc_connection_interface_aliasing1_return_from_request_aliases (
        context, aliases);
    g_free (aliases);
}

struct _g_hash_table_foreach_all_in_my_brain
{
    HazeConnection *conn;
    TpHandleRepoIface *contact_handles;
    GError **error;
};

static void
set_alias_success_cb (PurpleAccount *account,
                       const char *new_alias)
{
    TpBaseConnection *base_conn;
    GHashTable *aliases;

    DEBUG ("purple_account_set_public_alias succeeded, new alias %s",
        new_alias);

    base_conn = ACCOUNT_GET_TP_BASE_CONNECTION (account);

    aliases = g_hash_table_new (NULL, NULL);
    g_hash_table_insert (aliases,
        GUINT_TO_POINTER (tp_base_connection_get_self_handle (base_conn)),
        (gchar *) new_alias);

    tp_svc_connection_interface_aliasing1_emit_aliases_changed (base_conn,
        aliases);

    g_hash_table_unref (aliases);
}

static void
set_alias_failure_cb (PurpleAccount *account,
                      const char *error)
{
    DEBUG ("couldn't set alias: %s\n", error);
}

static void
set_aliases_foreach (gpointer key,
                     gpointer value,
                     gpointer user_data)
{
    struct _g_hash_table_foreach_all_in_my_brain *data =
        (struct _g_hash_table_foreach_all_in_my_brain *)user_data;
    GError *error = NULL;
    TpHandle handle = GPOINTER_TO_INT (key);
    gchar *new_alias = (gchar *)value;

    g_assert (can_alias (data->conn));

    if (!tp_handle_is_valid (data->contact_handles, handle, &error))
    {
        /* stop already */
    }
    else if (handle == tp_base_connection_get_self_handle (
          TP_BASE_CONNECTION (data->conn)))
    {
        DEBUG ("setting alias for myself to \"%s\"", new_alias);
        purple_account_set_public_alias (data->conn->account,
                                         new_alias,
                                         set_alias_success_cb,
                                         set_alias_failure_cb);
    }
    else
    {
        const gchar *bname = tp_handle_inspect (data->contact_handles, handle);
        PurpleBuddy *buddy = purple_find_buddy (data->conn->account, bname);

        if (buddy == NULL)
        {
            DEBUG ("can't set alias for %s to \"%s\": not on contact list",
                bname, new_alias);
            g_set_error (&error, TP_ERROR, TP_ERROR_NOT_IMPLEMENTED,
                "can't set alias for %s to \"%s\": not on contact list",
                bname, new_alias);
        }
        else
        {
            DEBUG ("setting alias for %s to \"%s\"", bname, new_alias);
            purple_blist_alias_buddy (buddy, new_alias);
            serv_alias_buddy (buddy);
        }
    }

    if (error) {
        if (*(data->error) == NULL)
        {
            /* No previous error. */
            *(data->error) = error;
        }
        else
        {
            g_error_free (error);
        }
    }

    return;
}

static void
haze_connection_set_aliases (TpSvcConnectionInterfaceAliasing1 *self,
                             GHashTable *aliases,
                             DBusGMethodInvocation *context)
{
    HazeConnection *conn = HAZE_CONNECTION (self);
    TpBaseConnection *base = TP_BASE_CONNECTION (conn);
    GError *error = NULL;
    struct _g_hash_table_foreach_all_in_my_brain data = { conn, NULL, &error };
    data.contact_handles =
        tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);

    if (!can_alias (conn))
    {
        g_set_error (&error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
            "You can't set aliases on this protocol");
        dbus_g_method_return_error (context, error);
        g_error_free (error);
        return;
    }

    g_hash_table_foreach (aliases, set_aliases_foreach, &data);

    if (error)
    {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
    }
    else
    {
        tp_svc_connection_interface_aliasing1_return_from_set_aliases (context);
    }

}

void
haze_connection_aliasing_iface_init (gpointer g_iface,
                                     gpointer iface_data)
{
    TpSvcConnectionInterfaceAliasing1Class *klass =
        (TpSvcConnectionInterfaceAliasing1Class *) g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_aliasing1_implement_##x (\
    klass, haze_connection_##x)
    IMPLEMENT(request_aliases);
    IMPLEMENT(set_aliases);
#undef IMPLEMENT
}

static void
blist_node_aliased_cb (PurpleBlistNode *node,
                       const char *old_alias,
                       gpointer unused)
{
    PurpleBuddy *buddy;
    TpBaseConnection *base_conn;
    GHashTable *aliases;
    TpHandle handle;
    TpHandleRepoIface *contact_handles;

    if (!PURPLE_BLIST_NODE_IS_BUDDY (node))
        return;

    buddy = (PurpleBuddy *)node;
    base_conn = ACCOUNT_GET_TP_BASE_CONNECTION (buddy->account);
    contact_handles =
        tp_base_connection_get_handles (base_conn, TP_HANDLE_TYPE_CONTACT);
    handle = tp_handle_ensure (contact_handles, buddy->name, NULL, NULL);

    aliases = g_hash_table_new (NULL, NULL);
    g_hash_table_insert (aliases,
        GUINT_TO_POINTER (handle),
        (gchar *) purple_buddy_get_alias (buddy));

    tp_svc_connection_interface_aliasing1_emit_aliases_changed (base_conn,
        aliases);

    g_hash_table_unref (aliases);
}

void
haze_connection_aliasing_class_init (GObjectClass *object_class)
{
    void *blist_handle = purple_blist_get_handle ();

    purple_signal_connect (blist_handle, "blist-node-aliased", object_class,
        PURPLE_CALLBACK (blist_node_aliased_cb), NULL);
}

gboolean
haze_connection_aliasing_fill_contact_attributes (HazeConnection *self,
    const gchar *dbus_interface,
    TpHandle handle,
    TpContactAttributeMap *attributes)
{
    if (!tp_strdiff (dbus_interface, TP_IFACE_CONNECTION_INTERFACE_ALIASING1))
    {
        GValue *value = tp_g_value_slice_new (G_TYPE_STRING);

        g_value_set_string (value, get_alias (self, handle));

        tp_contact_attribute_map_take_sliced_gvalue (attributes, handle,
            TP_TOKEN_CONNECTION_INTERFACE_ALIASING1_ALIAS, value);
        return TRUE;
    }

    return FALSE;
}
