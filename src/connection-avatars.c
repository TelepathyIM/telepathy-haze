#include <telepathy-glib/svc-connection.h>

#include "connection-avatars.h"
#include "connection.h"

void
haze_connection_get_avatar_requirements (TpSvcConnectionInterfaceAvatars *self,
                                         DBusGMethodInvocation *context)
{
    HazeConnection *conn = HAZE_CONNECTION (self);
    TpBaseConnection *base = TP_BASE_CONNECTION (conn);
    PurplePluginProtocolInfo *prpl_info;
    PurpleBuddyIconSpec *icon_spec;
    gchar **mime_types, **i;
    gchar *format;

    TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

    prpl_info = HAZE_CONNECTION_GET_PRPL_INFO (conn);
    icon_spec = &(prpl_info->icon_spec);

    /* If the spec or the formats are null, this iface wasn't implemented. */
    g_assert (icon_spec != NULL && icon_spec->format != NULL);

    mime_types = g_strsplit (icon_spec->format, ",", 0);

    for (i = mime_types; *i != NULL; i++)
    {
        format = *i;
        /* FIXME: image/ico is not the correct mime type. */
        *i = g_strconcat ("image/", format, NULL);
        g_free (format);
    }

    tp_svc_connection_interface_avatars_return_from_get_avatar_requirements (
        context, (const gchar **) mime_types,
        icon_spec->min_width, icon_spec->min_height,
        icon_spec->max_width, icon_spec->max_height,
        icon_spec->max_filesize);
    g_strfreev (mime_types);
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
        PurpleBuddyIcon *icon = purple_buddy_get_icon (buddy);
        if (icon)
        {
            icon_data = purple_buddy_icon_get_data (icon, &icon_size);
        }
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

    if (avatar)
    {
        token = purple_util_get_image_filename (avatar->data, avatar->len);
    }
    else
    {
        token = g_strdup ("");
    }

    g_assert (token);

    return token;
}

static gchar *
get_handle_token (HazeConnection *conn,
                  TpHandle handle)
{
    GArray *avatar = get_avatar (conn, handle);
    gchar *token = get_token (avatar);
    if (avatar)
        g_array_free (avatar, TRUE);
    return token;
}

void
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

void
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
        g_debug ("returning avatar for %u, length %u", contact, avatar->len);
        tp_svc_connection_interface_avatars_return_from_request_avatar (
            context, avatar, "" /* no way to get MIME type from purple */);
        g_array_free (avatar, TRUE);
    }
    else
    {
        g_debug ("handle %u has no avatar", contact);
        gchar *message = g_strdup_printf ("handle %u has no avatar", contact);
        g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE, message);

        dbus_g_method_return_error (context, error);

        g_free (error);
        g_free (message);
    }
}

void
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
        if (avatar)
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

void
haze_connection_clear_avatar (TpSvcConnectionInterfaceAvatars *self,
                              DBusGMethodInvocation *context)
{
    GError *error = NULL;
    g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
        "Haven't got around to dealing with your own avatar yet");
    dbus_g_method_return_error (context, error);
    g_free (error);
/*    tp_svc_connection_interface_avatars_return_from_clear_avatar (context);*/
}

void
haze_connection_set_avatar (TpSvcConnectionInterfaceAvatars *self,
                            const GArray *avatar,
                            const gchar *mime_type,
                            DBusGMethodInvocation *context)
{
    GError *error = NULL;
    g_set_error (&error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
        "Haven't got around to dealing with your own avatar yet");
    dbus_g_method_return_error (context, error);
    g_free (error);
//  tp_svc_connection_interface_avatars_return_from_set_avatar (context, token);
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
    IMPLEMENT(request_avatar);
    IMPLEMENT(request_avatars);
    IMPLEMENT(set_avatar);
    IMPLEMENT(clear_avatar);
#undef IMPLEMENT
}

void
buddy_icon_changed_cb (PurpleBuddy *buddy, gpointer unused)
{
    HazeConnection *conn = ACCOUNT_GET_HAZE_CONNECTION (buddy->account);
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
    TpHandleRepoIface *contact_repo =
        tp_base_connection_get_handles (base_conn, TP_HANDLE_TYPE_CONTACT);

    const char* bname = purple_buddy_get_name (buddy);
    TpHandle contact = tp_handle_ensure (contact_repo, bname, NULL, NULL);

    gchar *token = get_handle_token (conn, contact);

    g_debug ("buddy_icon_changed_cb: %s '%s'", bname, token);

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
