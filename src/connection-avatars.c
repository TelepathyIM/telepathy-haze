/*
 * connection-avatars.c - Avatars interface implementation of HazeConnection
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

#include "connection-avatars.h"

#include <string.h>

#include <telepathy-glib/contacts-mixin.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-connection.h>

#include <libpurple/cipher.h>

#include "connection.h"
#include "debug.h"

static gchar **
_get_acceptable_mime_types (HazeConnection *self)
{
    PurplePluginProtocolInfo *prpl_info = HAZE_CONNECTION_GET_PRPL_INFO (self);

    g_return_val_if_fail (prpl_info->icon_spec.format != NULL, NULL);

    if (self->acceptable_avatar_mime_types == NULL)
    {
        gchar **mime_types, **i;
        gchar *format;

        mime_types = g_strsplit (prpl_info->icon_spec.format, ",", 0);

        for (i = mime_types; *i != NULL; i++)
        {
            format = *i;
            /* FIXME: image/ico is not the correct mime type. */
            *i = g_strconcat ("image/", format, NULL);
            g_free (format);
        }

        self->acceptable_avatar_mime_types = mime_types;
    }

    return self->acceptable_avatar_mime_types;
}

static void
haze_connection_get_avatar_requirements (TpSvcConnectionInterfaceAvatars *self,
                                         DBusGMethodInvocation *context)
{
    HazeConnection *conn = HAZE_CONNECTION (self);
    TpBaseConnection *base = TP_BASE_CONNECTION (conn);
    PurplePluginProtocolInfo *prpl_info;
    PurpleBuddyIconSpec *icon_spec;

    TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

    prpl_info = HAZE_CONNECTION_GET_PRPL_INFO (conn);
    icon_spec = &(prpl_info->icon_spec);

    /* If the spec or the formats are null, this iface wasn't implemented. */
    g_assert (icon_spec != NULL && icon_spec->format != NULL);

    tp_svc_connection_interface_avatars_return_from_get_avatar_requirements (
        context, (const gchar **) _get_acceptable_mime_types (conn),
        icon_spec->min_width, icon_spec->min_height,
        icon_spec->max_width, icon_spec->max_height,
        icon_spec->max_filesize);
}

static GArray *
get_avatar (HazeConnection *conn,
            TpHandle handle)
{
    GArray *avatar = NULL;
    TpBaseConnection *base = TP_BASE_CONNECTION (conn);
    TpHandleRepoIface *contact_handles =
        tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);
    gconstpointer icon_data = NULL;
    size_t icon_size = 0;
    if (handle == base->self_handle)
    {
        PurpleStoredImage *image =
            purple_buddy_icons_find_account_icon (conn->account);
        if (image)
        {
            icon_data = purple_imgstore_get_data (image);
            icon_size = purple_imgstore_get_size (image);
        }
    }
    else
    {
        const gchar *bname = tp_handle_inspect (contact_handles, handle);
        PurpleBuddy *buddy = purple_find_buddy (conn->account, bname);
        PurpleBuddyIcon *icon = NULL;
        if (buddy)
            icon = purple_buddy_get_icon (buddy);
        if (icon)
            icon_data = purple_buddy_icon_get_data (icon, &icon_size);
    }

    if (icon_data)
    {
        avatar = g_array_sized_new (FALSE, FALSE, sizeof (gchar), icon_size);
        g_array_append_vals (avatar, icon_data, icon_size);
    }

    return avatar;
}

static gchar *
get_token (const GArray *avatar)
{
    gchar *token;

    PurpleCipherContext *context;
    gchar digest[41];

    g_assert (avatar != NULL);

    context = purple_cipher_context_new_by_name ("sha1", NULL);
    if (context == NULL)
    {
        g_error ("Could not find libpurple's sha1 cipher");
    }

    /* Hash the image data */
    purple_cipher_context_append (context, (const guchar *) avatar->data,
            avatar->len);
    if (!purple_cipher_context_digest_to_str (context, sizeof (digest),
                digest, NULL))
    {
        g_error ("Failed to get SHA-1 digest");
    }
    purple_cipher_context_destroy (context);

    token = g_strdup (digest);

    return token;
}

static gchar *
get_handle_token (HazeConnection *conn,
                  TpHandle handle)
{
    GArray *avatar = get_avatar (conn, handle);
    gchar *token;

    if (avatar != NULL)
    {
        token = get_token (avatar);
        g_array_free (avatar, TRUE);
    }
    else
    {
        token = g_strdup ("");
    }

    return token;
}

static void
haze_connection_get_avatar_tokens (TpSvcConnectionInterfaceAvatars *self,
                                   const GArray *contacts,
                                   DBusGMethodInvocation *context)
{
    gchar **icons;
    HazeConnection *conn = HAZE_CONNECTION (self);
    TpBaseConnection *base = TP_BASE_CONNECTION (self);
    guint i;

    TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

    icons = g_new0 (gchar *, contacts->len + 1);
    for (i = 0; i < contacts->len; i++)
    {
        TpHandle handle = g_array_index (contacts, TpHandle, i);
        icons[i] = get_handle_token (conn, handle);
    }

    tp_svc_connection_interface_avatars_return_from_get_avatar_tokens (
        context, (const gchar **) icons);
    g_strfreev (icons);
}

static void
haze_connection_get_known_avatar_tokens (TpSvcConnectionInterfaceAvatars *self,
                                         const GArray *contacts,
                                         DBusGMethodInvocation *context)
{
    HazeConnection *conn = HAZE_CONNECTION (self);
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
    GHashTable *tokens;
    guint i;
    GError *err = NULL;

    TpHandleRepoIface *contact_repo =
        tp_base_connection_get_handles (base_conn, TP_HANDLE_TYPE_CONTACT);

    if (!tp_handles_are_valid (contact_repo, contacts, FALSE, &err))
    {
        dbus_g_method_return_error (context, err);
        g_error_free (err);
        return;
    }

    tokens = g_hash_table_new_full (NULL, NULL, NULL, g_free);

    for (i = 0; i < contacts->len; i++)
    {
        TpHandle handle = g_array_index (contacts, TpHandle, i);
        gchar *token = NULL;

        /* Purple doesn't provide any way to distinguish between a contact with
         * no avatar and a contact whose avatar we haven't retrieved yet,
         * mainly because it always automatically downloads all avatars.  So in
         * general, we assume no avatar means the former, so that clients don't
         * repeatedly call RequestAvatar hoping eventually to get the avatar.
         *
         * But on protocols where avatars aren't saved server-side, we should
         * report that it's unknown, so that the UI (aka. mcd) can re-set the
         * avatar you last used.  So we special-case self_handle here.
         */

        if (handle == base_conn->self_handle)
        {
            GArray *avatar = get_avatar (conn, handle);
            if (avatar != NULL)
            {
                token = get_token (avatar);
                g_array_free (avatar, TRUE);
            }
        }
        else
        {
            token = get_handle_token (conn, handle);
        }

        if (token != NULL)
            g_hash_table_insert (tokens, GUINT_TO_POINTER (handle), token);
    }

    tp_svc_connection_interface_avatars_return_from_get_known_avatar_tokens (
        context, tokens);

    g_hash_table_unref (tokens);
}

static void
haze_connection_request_avatar (TpSvcConnectionInterfaceAvatars *self,
                                guint contact,
                                DBusGMethodInvocation *context)
{
    HazeConnection *conn = HAZE_CONNECTION (self);
    TpBaseConnection *base = TP_BASE_CONNECTION (conn);
    GArray *avatar;
    GError *error = NULL;

    TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

    avatar = get_avatar (conn, contact);
    if (avatar)
    {
        DEBUG ("returning avatar for %u, length %u", contact, avatar->len);
        tp_svc_connection_interface_avatars_return_from_request_avatar (
            context, avatar, "" /* no way to get MIME type from purple */);
        g_array_free (avatar, TRUE);
    }
    else
    {
        DEBUG ("handle %u has no avatar", contact);
        g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                     "handle %u has no avatar", contact);


        dbus_g_method_return_error (context, error);

        g_error_free (error);
    }
}

static void
haze_connection_request_avatars (TpSvcConnectionInterfaceAvatars *self,
                                 const GArray *contacts,
                                 DBusGMethodInvocation *context)
{
    HazeConnection *conn = HAZE_CONNECTION (self);
    TpBaseConnection *base = TP_BASE_CONNECTION (conn);
    guint i;

    TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

    for (i = 0; i < contacts->len; i++)
    {
        TpHandle handle = g_array_index (contacts, TpHandle, i);
        GArray *avatar = get_avatar (conn, handle);
        if (avatar != NULL)
        {
            gchar *token = get_token (avatar);
            tp_svc_connection_interface_avatars_emit_avatar_retrieved (
                conn, handle, token, avatar, "" /* unknown MIME type */);
            g_free (token);
            g_array_free (avatar, TRUE);
        }
    }

    tp_svc_connection_interface_avatars_return_from_request_avatars (context);
}

static void
haze_connection_clear_avatar (TpSvcConnectionInterfaceAvatars *self,
                              DBusGMethodInvocation *context)
{
    HazeConnection *conn = HAZE_CONNECTION (self);
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
    PurpleAccount *account = conn->account;

    purple_buddy_icons_set_account_icon (account, NULL, 0);

    tp_svc_connection_interface_avatars_return_from_clear_avatar (context);
    tp_svc_connection_interface_avatars_emit_avatar_updated (conn,
        base_conn->self_handle, "");
}

static void
haze_connection_set_avatar (TpSvcConnectionInterfaceAvatars *self,
                            const GArray *avatar,
                            const gchar *mime_type,
                            DBusGMethodInvocation *context)
{
    HazeConnection *conn = HAZE_CONNECTION (self);
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
    PurpleAccount *account = conn->account;
    PurplePluginProtocolInfo *prpl_info = HAZE_CONNECTION_GET_PRPL_INFO (conn);

    GError *error = NULL;

    guchar *icon_data = NULL;
    size_t icon_len = avatar->len;
    gchar *token;
    gchar **mime_types = _get_acceptable_mime_types (conn);

    gboolean acceptable_mime_type = FALSE;

    const size_t max_filesize = prpl_info->icon_spec.max_filesize;

    if (max_filesize > 0 && icon_len > max_filesize)
    {
        g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                     "avatar is %" G_GSIZE_FORMAT "B, "
                     "but the limit is %" G_GSIZE_FORMAT "B",
                     icon_len, max_filesize);

        dbus_g_method_return_error (context, error);

        g_error_free (error);
        return;
    }

    /* FIXME: This is a work-around for mission control passing an empty
     *        mime_type when it re-sets your avatar on connection.  Since it
     *        only caches the avatar if it was set correctly by Empathy, it's
     *        most likely actually acceptable, but the work-around should
     *        probably go away when MC is fixed.
     */
    if (*mime_type == '\0')
        acceptable_mime_type = TRUE;

    while (!acceptable_mime_type && *mime_types != NULL)
    {
        if (!tp_strdiff (*mime_types, mime_type))
            acceptable_mime_type = TRUE;
        mime_types++;
    }

    if (!acceptable_mime_type)
    {
        g_set_error (&error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
            "'%s' is not a supported MIME type", mime_type);

        dbus_g_method_return_error (context, error);

        g_error_free (error);

        return;
    }


    /* purple_buddy_icons_set_account_icon () takes ownership of the pointer
     * passed to it, but 'avatar' will be freed soon.
     */
    icon_data = g_malloc (avatar->len);
    memcpy (icon_data, avatar->data, icon_len);
    purple_buddy_icons_set_account_icon (account, icon_data, icon_len);
    token = get_token (avatar);
    DEBUG ("%s", token);

    tp_svc_connection_interface_avatars_return_from_set_avatar (context, token);
    tp_svc_connection_interface_avatars_emit_avatar_updated (conn,
        base_conn->self_handle, token);
    g_free (token);
}

void
haze_connection_avatars_iface_init (gpointer g_iface,
                                    gpointer iface_data)
{
    TpSvcConnectionInterfaceAvatarsClass *klass =
        (TpSvcConnectionInterfaceAvatarsClass *) g_iface;

#define IMPLEMENT(x) tp_svc_connection_interface_avatars_implement_##x (\
    klass, haze_connection_##x)
    IMPLEMENT(get_avatar_requirements);
    IMPLEMENT(get_avatar_tokens);
    IMPLEMENT(get_known_avatar_tokens);
    IMPLEMENT(request_avatar);
    IMPLEMENT(request_avatars);
    IMPLEMENT(set_avatar);
    IMPLEMENT(clear_avatar);
#undef IMPLEMENT
}

static void
buddy_icon_changed_cb (PurpleBuddy *buddy,
                       gpointer unused)
{
    HazeConnection *conn = ACCOUNT_GET_HAZE_CONNECTION (buddy->account);
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
    TpHandleRepoIface *contact_repo =
        tp_base_connection_get_handles (base_conn, TP_HANDLE_TYPE_CONTACT);

    const char* bname = purple_buddy_get_name (buddy);
    TpHandle contact = tp_handle_ensure (contact_repo, bname, NULL, NULL);

    gchar *token = get_handle_token (conn, contact);

    DEBUG ("%s '%s'", bname, token);

    tp_svc_connection_interface_avatars_emit_avatar_updated (conn, contact,
        token);
    tp_handle_unref (contact_repo, contact);
    g_free (token);
}

void
haze_connection_avatars_class_init (GObjectClass *object_class)
{
    void *blist_handle = purple_blist_get_handle ();

    purple_signal_connect (blist_handle, "buddy-icon-changed", object_class,
        PURPLE_CALLBACK (buddy_icon_changed_cb), NULL);
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
        gchar *token = get_handle_token (self, handle);
        GValue *value = tp_g_value_slice_new (G_TYPE_STRING);

        g_assert (token != NULL);
        g_value_set_string (value, token);

        /* this steals the GValue */
        tp_contacts_mixin_set_contact_attribute (attributes_hash, handle,
            TP_IFACE_CONNECTION_INTERFACE_AVATARS "/token", value);
    }
}

void
haze_connection_avatars_init (GObject *object)
{
    tp_contacts_mixin_add_contact_attributes_iface (object,
        TP_IFACE_CONNECTION_INTERFACE_AVATARS,
        fill_contact_attributes);
}
