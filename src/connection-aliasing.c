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
#include "connection-aliasing.h"

#include <telepathy-glib/contacts-mixin.h>
#include <telepathy-glib/handle.h>
#include <telepathy-glib/interfaces.h>

#include "connection.h"
#include "debug.h"

#define HAZE_TP_ALIAS_PAIR_TYPE (dbus_g_type_get_struct ("GValueArray", \
      G_TYPE_UINT, G_TYPE_STRING, G_TYPE_INVALID))

static gboolean
can_alias (HazeConnection *conn)
{
    PurplePluginProtocolInfo *prpl;

    g_assert (!purple_account_is_disconnected (conn->account));

    prpl = HAZE_CONNECTION_GET_PRPL_INFO (conn);

    return (prpl->alias_buddy != NULL);
}

static void
haze_connection_get_alias_flags (TpSvcConnectionInterfaceAliasing *self,
                                 DBusGMethodInvocation *context)
{
    TpBaseConnection *base = TP_BASE_CONNECTION (self);
    HazeConnection *conn = HAZE_CONNECTION (base);
    guint flags = 0;

    TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

    if (can_alias (conn))
    {
        flags = TP_CONNECTION_ALIAS_FLAG_USER_SET;
    }

    DEBUG ("alias flags: %u", flags);
    tp_svc_connection_interface_aliasing_return_from_get_alias_flags (
            context, flags);
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

    if (handle == base->self_handle)
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
haze_connection_get_aliases (TpSvcConnectionInterfaceAliasing *self,
                             const GArray *contacts,
                             DBusGMethodInvocation *context)
{
    HazeConnection *conn = HAZE_CONNECTION (self);
    TpBaseConnection *base = TP_BASE_CONNECTION (conn);
    TpHandleRepoIface *contact_handles =
        tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);
    guint i;
    GError *error = NULL;
    GHashTable *aliases;

    if (!tp_handles_are_valid (contact_handles, contacts, FALSE, &error))
    {
        dbus_g_method_return_error (context, error);
        g_error_free (error);
        return;
    }

    aliases = g_hash_table_new (NULL, NULL);

    for (i = 0; i < contacts->len; i++)
    {
        TpHandle handle = g_array_index (contacts, TpHandle, i);

        g_hash_table_insert (aliases, GUINT_TO_POINTER (handle),
            (gchar *) get_alias (conn, handle));
    }

    tp_svc_connection_interface_aliasing_return_from_get_aliases (
        context, aliases);
    g_hash_table_destroy (aliases);
}

static void
haze_connection_request_aliases (TpSvcConnectionInterfaceAliasing *self,
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

    tp_svc_connection_interface_aliasing_return_from_request_aliases (
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
set_aliases_foreach (gpointer key,
                     gpointer value,
                     gpointer user_data)
{
    struct _g_hash_table_foreach_all_in_my_brain *data =
        (struct _g_hash_table_foreach_all_in_my_brain *)user_data;
    GError *error = NULL;
    TpHandle handle = GPOINTER_TO_INT (key);
    gchar *new_alias = (gchar *)value;
    const gchar *bname = tp_handle_inspect (data->contact_handles, handle);

    g_assert (can_alias (data->conn));

    DEBUG ("setting alias for %s to \"%s\"", bname, new_alias);

    if (!tp_handle_is_valid (data->contact_handles, handle, &error))
    {
        /* stop already */
    }
    else if (handle == TP_BASE_CONNECTION (data->conn)->self_handle)
    {
        g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
            "Sadly, there's no general API in libpurple to set your own "
            "server alias.");
    }
    else
    {
        PurpleBuddy *buddy = purple_find_buddy (data->conn->account, bname);

        if (buddy == NULL)
        {
            g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
                "You can't set the alias of someone not on your contact list");
        }
        else
        {
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
haze_connection_set_aliases (TpSvcConnectionInterfaceAliasing *self,
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
        g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
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
        tp_svc_connection_interface_aliasing_return_from_set_aliases (context);
    }

}

void
haze_connection_aliasing_iface_init (gpointer g_iface,
                                     gpointer iface_data)
{
    TpSvcConnectionInterfaceAliasingClass *klass =
        (TpSvcConnectionInterfaceAliasingClass *) g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_aliasing_implement_##x (\
    klass, haze_connection_##x)
    IMPLEMENT(get_alias_flags);
    IMPLEMENT(get_aliases);
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
    GPtrArray *aliases;
    GValue entry = {0, };
    TpHandle handle;
    TpHandleRepoIface *contact_handles;

    if (!PURPLE_BLIST_NODE_IS_BUDDY (node))
        return;

    buddy = (PurpleBuddy *)node;
    base_conn = ACCOUNT_GET_TP_BASE_CONNECTION (buddy->account);
    contact_handles =
        tp_base_connection_get_handles (base_conn, TP_HANDLE_TYPE_CONTACT);
    handle = tp_handle_ensure (contact_handles, buddy->name, NULL, NULL);

    g_value_init (&entry, HAZE_TP_ALIAS_PAIR_TYPE);
    g_value_take_boxed (&entry,
        dbus_g_type_specialized_construct (HAZE_TP_ALIAS_PAIR_TYPE));

    dbus_g_type_struct_set (&entry,
        0, handle,
        1, purple_buddy_get_alias (buddy),
        G_MAXUINT);

    aliases = g_ptr_array_sized_new (1);
    g_ptr_array_add (aliases, g_value_get_boxed (&entry));

    tp_svc_connection_interface_aliasing_emit_aliases_changed (base_conn,
        aliases);

    g_value_unset (&entry);
    g_ptr_array_free (aliases, TRUE);
    tp_handle_unref (contact_handles, handle);
}

void
haze_connection_aliasing_class_init (GObjectClass *object_class)
{
    void *blist_handle = purple_blist_get_handle ();

    purple_signal_connect (blist_handle, "blist-node-aliased", object_class,
        PURPLE_CALLBACK (blist_node_aliased_cb), NULL);
}

static void
fill_contact_attributes (GObject *object,
                         const GArray *contacts,
                         GHashTable *attributes_hash)
{
    HazeConnection *self = HAZE_CONNECTION (object);
    guint i;

    for (i = 0; i < contacts->len; i++)
    {
        TpHandle handle = g_array_index (contacts, guint, i);
        GValue *value = tp_g_value_slice_new (G_TYPE_STRING);

        g_value_set_string (value, get_alias (self, handle));

        /* this steals the GValue */
        tp_contacts_mixin_set_contact_attribute (attributes_hash, handle,
            TP_IFACE_CONNECTION_INTERFACE_ALIASING "/alias", value);
    }
}

void
haze_connection_aliasing_init (GObject *object)
{
    tp_contacts_mixin_add_contact_attributes_iface (object,
        TP_IFACE_CONNECTION_INTERFACE_ALIASING,
        fill_contact_attributes);
}
