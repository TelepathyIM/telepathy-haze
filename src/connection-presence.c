/*
 * connection-presence.c - Presence interface implementation of HazeConnection
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

#include "connection-presence.h"
#include "debug.h"

#include <telepathy-glib/dbus.h>

static const TpPresenceStatusOptionalArgumentSpec arg_specs[] = {
    { "message", "s" },
    { NULL, NULL }
};

typedef enum {
    HAZE_STATUS_AVAILABLE = 0,
    HAZE_STATUS_BUSY,
    HAZE_STATUS_AWAY,
    HAZE_STATUS_EXT_AWAY,
    HAZE_STATUS_INVISIBLE,
    HAZE_STATUS_OFFLINE,

    HAZE_NUM_STATUSES
} HazeStatusIndex;

static const TpPresenceStatusSpec statuses[] = {
    { "available", TP_CONNECTION_PRESENCE_TYPE_AVAILABLE, TRUE,
        arg_specs, NULL, NULL },
    { "busy", TP_CONNECTION_PRESENCE_TYPE_BUSY, TRUE,
        arg_specs, NULL, NULL },
    { "away", TP_CONNECTION_PRESENCE_TYPE_AWAY, TRUE,
        arg_specs, NULL, NULL },
    { "ext_away", TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY, TRUE,
        arg_specs, NULL, NULL },
    { "invisible", TP_CONNECTION_PRESENCE_TYPE_HIDDEN, TRUE, NULL, NULL, NULL },
    { "offline", TP_CONNECTION_PRESENCE_TYPE_OFFLINE, FALSE, NULL, NULL, NULL },
    { NULL, TP_CONNECTION_PRESENCE_TYPE_UNSET, FALSE, NULL, NULL, NULL }
};

/* Indexed by HazeStatusIndex */
static const PurpleStatusPrimitive primitives[] = {
    PURPLE_STATUS_AVAILABLE,
    PURPLE_STATUS_UNAVAILABLE,
    PURPLE_STATUS_AWAY,
    PURPLE_STATUS_EXTENDED_AWAY,
    PURPLE_STATUS_INVISIBLE,
    PURPLE_STATUS_OFFLINE
};

/* Indexed by PurpleStatusPrimitive */
static const HazeStatusIndex status_indices[] = {
    HAZE_NUM_STATUSES,     /* invalid! */
    HAZE_STATUS_OFFLINE,   /* PURPLE_STATUS_OFFLINE */
    HAZE_STATUS_AVAILABLE, /* PURPLE_STATUS_AVAILABLE */
    HAZE_STATUS_BUSY,      /* PURPLE_STATUS_UNAVAILABLE */
    HAZE_STATUS_INVISIBLE, /* PURPLE_STATUS_INVISIBLE */
    HAZE_STATUS_AWAY,      /* PURPLE_STATUS_AWAY */
    HAZE_STATUS_EXT_AWAY   /* PURPLE_STATUS_EXTENDED_AWAY */
};

static TpPresenceStatus *
_get_tp_status (PurpleStatus *p_status)
{
    PurpleStatusType *type;
    PurpleStatusPrimitive prim;
    GHashTable *arguments = g_hash_table_new_full (g_str_hash, g_str_equal,
        NULL, (GDestroyNotify) tp_g_value_slice_free);
    guint status_ix = -1;
    const gchar *xhtml_message;
    gchar *message;
    TpPresenceStatus *tp_status;

    g_assert (p_status != NULL);

    type = purple_status_get_type (p_status);
    prim = purple_status_type_get_primitive (type);
    status_ix = status_indices[prim];

    xhtml_message = purple_status_get_attr_string (p_status, "message");
    if (xhtml_message)
    {
        GValue *message_v = g_slice_new0 (GValue);

        message = purple_markup_strip_html (xhtml_message);
        g_value_init (message_v, G_TYPE_STRING);
        g_value_set_string (message_v, message);
        g_hash_table_insert (arguments, "message", message_v);
        g_free (message);
    }

    tp_status = tp_presence_status_new (status_ix, arguments);
    g_hash_table_destroy (arguments);
    return tp_status;
}

static const char *
_get_purple_status_id (HazeConnection *self,
                       guint index)
{
    PurpleStatusPrimitive prim = PURPLE_STATUS_UNSET;
    PurpleStatusType *type;

    g_assert (index < HAZE_NUM_STATUSES);
    prim = primitives[index];

    type = purple_account_get_status_type_with_primitive (self->account, prim);
    if (type)
    {
        return (purple_status_type_get_id (type));
    }
    else
    {
        return NULL;
    }
}

static gboolean
_status_available (GObject *obj,
                   guint index)
{
    HazeConnection *self = HAZE_CONNECTION (obj);
    /* FIXME: (a) should we be able to set offline on ourselves;
     *        (b) deal with some protocols not having status messages.
     */
    return (_get_purple_status_id (self, index) != NULL);
}


static GHashTable *
_get_contact_statuses (GObject *obj,
                       const GArray *contacts,
                       GError **error)
{
    GHashTable *status_table = g_hash_table_new_full (g_direct_hash,
        g_direct_equal, NULL, NULL);
    HazeConnection *conn = HAZE_CONNECTION (obj);
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (obj);
    TpHandleRepoIface *handle_repo =
        tp_base_connection_get_handles (base_conn, TP_HANDLE_TYPE_CONTACT);
    guint i;

    for (i = 0; i < contacts->len; i++)
    {
        TpHandle handle = g_array_index (contacts, TpHandle, i);
        const gchar *bname;
        TpPresenceStatus *tp_status;
        PurpleBuddy *buddy;
        PurpleStatus *p_status;

        g_assert (tp_handle_is_valid (handle_repo, handle, NULL));

        if (handle == base_conn->self_handle)
        {
            p_status = purple_account_get_active_status (conn->account);
        }
        else
        {
            bname = tp_handle_inspect (handle_repo, handle);
            buddy = purple_find_buddy (conn->account, bname);

            if (buddy)
            {
                PurplePresence *presence = purple_buddy_get_presence (buddy);
                p_status = purple_presence_get_active_status (presence);
            }
            else
            {
                DEBUG ("[%s] %s isn't on the blist, ergo no status!",
                         conn->account->username, bname);
                g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                    "Presence for %u unknown; subscribe to them first", handle);
                g_hash_table_destroy (status_table);
                status_table = NULL;
                break;
            }
        }

        tp_status = _get_tp_status (p_status);

        g_hash_table_insert (status_table, GINT_TO_POINTER (handle), tp_status);
    }

    return status_table;
}

void
haze_connection_presence_account_status_changed (PurpleAccount *account,
                                                 PurpleStatus *status)
{
    TpBaseConnection *base_conn;
    TpPresenceStatus *tp_status;

    /* This gets called as soon as the account is created, before we get a
     * chance to set ui_data.  This is a "shame".  (You'd think that an account
     * could not have a status before it is enabled, but you'd be "wrong".)
     */
    if (account->ui_data)
    {
        base_conn = ACCOUNT_GET_TP_BASE_CONNECTION (account);
        tp_status = _get_tp_status (status);

        tp_presence_mixin_emit_one_presence_update (G_OBJECT (base_conn),
            base_conn->self_handle, tp_status);
    }
}

static void
update_status (PurpleBuddy *buddy,
               PurpleStatus *status)
{
    PurpleAccount *account = purple_buddy_get_account (buddy);
    HazeConnection *conn = ACCOUNT_GET_HAZE_CONNECTION (account);
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
    TpHandleRepoIface *handle_repo =
        tp_base_connection_get_handles (base_conn, TP_HANDLE_TYPE_CONTACT);

    const gchar *bname = purple_buddy_get_name (buddy);
    TpHandle handle = tp_handle_ensure (handle_repo, bname, NULL, NULL);

    TpPresenceStatus *tp_status;

    DEBUG ("%s changed to status %s", bname, purple_status_get_id (status));

    tp_status = _get_tp_status (status);

    tp_presence_mixin_emit_one_presence_update (G_OBJECT (conn), handle,
        tp_status);
    tp_handle_unref (handle_repo, handle);
}

static void
status_changed_cb (PurpleBuddy *buddy,
                   PurpleStatus *old_status,
                   PurpleStatus *new_status,
                   gpointer unused)
{
    update_status (buddy, new_status);
}

static void
signed_on_off_cb (PurpleBuddy *buddy,
                  gpointer data)
{
    /*
    gboolean signed_on = GPOINTER_TO_INT (data);
    */
    PurplePresence *presence = purple_buddy_get_presence (buddy);
    update_status (buddy, purple_presence_get_active_status (presence));
}

static gboolean
_set_own_status (GObject *obj,
                 const TpPresenceStatus *status,
                 GError **error)
{
    HazeConnection *self = HAZE_CONNECTION (obj);
    const char *status_id = NULL;
    const gchar *message = NULL;
    GList *attrs = NULL;

    if (status != NULL)
      {
        status_id = _get_purple_status_id (self, status->index);

        if (status->optional_arguments != NULL)
          message = tp_asv_get_string (status->optional_arguments, "message");
      }

    if (status_id == NULL)
      {
        /* TODO: Is there a more sensible way to have a default? */
        DEBUG ("defaulting to 'available' status");
        status_id = "available";
      }

    if (message != NULL)
      {
        attrs = g_list_append (attrs, "message");
        attrs = g_list_append (attrs, (gchar *) message);
      }

    purple_account_set_status_list (self->account, status_id, TRUE, attrs);

    g_list_free (attrs);

    return TRUE;
}

void
haze_connection_presence_class_init (GObjectClass *object_class)
{
    void *blist_handle = purple_blist_get_handle ();

    purple_signal_connect (blist_handle, "buddy-status-changed", object_class,
        PURPLE_CALLBACK (status_changed_cb), NULL);
    purple_signal_connect (blist_handle, "buddy-signed-on", object_class,
        PURPLE_CALLBACK (signed_on_off_cb), GINT_TO_POINTER (TRUE));
    purple_signal_connect (blist_handle, "buddy-signed-off", object_class,
        PURPLE_CALLBACK (signed_on_off_cb), GINT_TO_POINTER (FALSE));

    tp_presence_mixin_class_init (object_class,
        G_STRUCT_OFFSET (HazeConnectionClass, presence_class),
        _status_available, _get_contact_statuses, _set_own_status, statuses);

    tp_presence_mixin_simple_presence_init_dbus_properties (object_class);
}

void
haze_connection_presence_init (GObject *object)
{
    tp_presence_mixin_init (object, G_STRUCT_OFFSET (HazeConnection,
        presence));
    tp_presence_mixin_simple_presence_register_with_contacts_mixin (object);
}
