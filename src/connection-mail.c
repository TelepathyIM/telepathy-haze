/*
 * haze-connection-mail.h - MailNotification interface implementation of
 *                          HazeConnection
 * Copyright (C) 2010 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "extensions/extensions.h"

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/svc-connection.h>

#include "connection.h"
#include "connection-mail.h"
#include "debug.h"


enum
{
    PROP_MAIL_NOTIFICATION_FLAGS,
    PROP_UNREAD_MAIL_COUNT,
    PROP_UNREAD_MAILS,
    PROP_MAIL_ADDRESS,
    NUM_OF_PROP,
};


static GPtrArray empty_array = { 0 };


static void
haze_connection_mail_subscribe (
        HazeSvcConnectionInterfaceMailNotification *iface,
        DBusGMethodInvocation *context)
{
    /* Nothing do do, no resources attached to mail notification */
    haze_svc_connection_interface_mail_notification_return_from_subscribe (
      context);
}


static void
haze_connection_mail_unsubscribe (
        HazeSvcConnectionInterfaceMailNotification *iface,
        DBusGMethodInvocation *context)
{
    /* Nothing do do, no resources attached to mail notification */
    haze_svc_connection_interface_mail_notification_return_from_unsubscribe (
        context);
}


static void
haze_connection_mail_request_inbox_url (
        HazeSvcConnectionInterfaceMailNotification *iface,
        DBusGMethodInvocation *context)
{
    GError e = {TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
        "LibPurple does not provide Inbox URL"};
    dbus_g_method_return_error (context, &e);
}


static void
haze_connection_mail_request_mail_url (
        HazeSvcConnectionInterfaceMailNotification *iface,
        const gchar *in_id,
        const GValue *in_url_data,
        DBusGMethodInvocation *context)
{
    GValueArray *result;

    if (!G_VALUE_HOLDS_STRING (in_url_data))
        {
             GError e = {TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
                 "Wrong type for url-data"};
             dbus_g_method_return_error (context, &e);
             return;
        }

    result = tp_value_array_build (3,
        G_TYPE_STRING, g_value_get_string (in_url_data),
        G_TYPE_UINT, HAZE_HTTP_METHOD_GET,
        HAZE_ARRAY_TYPE_HTTP_POST_DATA_LIST, &empty_array,
        G_TYPE_INVALID);

    haze_svc_connection_interface_mail_notification_return_from_request_inbox_url (
        context, result);

    g_value_array_free (result);
}


void
haze_connection_mail_init (GObject *conn)
{
    /* nothing to do */
}


void
haze_connection_mail_iface_init (gpointer g_iface,
        gpointer iface_data)
{
    HazeSvcConnectionInterfaceMailNotificationClass *klass = g_iface;

#define IMPLEMENT(x) haze_svc_connection_interface_mail_notification_implement_##x (\
        klass, haze_connection_mail_##x)
    IMPLEMENT(subscribe);
    IMPLEMENT(unsubscribe);
    IMPLEMENT(request_inbox_url);
    IMPLEMENT(request_mail_url);
#undef IMPLEMENT
}


static gchar *_get_email (PurpleAccount *account)
{
    const gchar *username = purple_account_get_username (account);
    gchar *email;
    if (!g_utf8_strchr (username, -1, '@'))
        {
            /* Here we have per-protocol quark for e-mail. */
            const gchar *protocol = purple_account_get_protocol_id (account);
            if (!tp_strdiff (protocol, "prpl-yahoo"))
                {
                    email = g_strdup_printf ("%s@yahoo.com", username);
                }
            else
                {
                    /* We make sure that the e-mail at least look like
                     * one and that it is discriminated by protocol */
                    email = g_strdup_printf ("%s@%s", username, protocol);
                }
        }
    else
       {
           email = g_strdup (username);
       }
    return email;
}


void
haze_connection_mail_properties_getter (GObject *object,
        GQuark interface,
        GQuark name,
        GValue *value,
        gpointer getter_data)
{
    static GQuark prop_quarks[NUM_OF_PROP] = {0};
    HazeConnection *conn = HAZE_CONNECTION (object);

    if (G_UNLIKELY (prop_quarks[0] == 0))
        {
            prop_quarks[PROP_MAIL_NOTIFICATION_FLAGS] =
                g_quark_from_static_string ("MailNotificationFlags");
            prop_quarks[PROP_UNREAD_MAIL_COUNT] =
                g_quark_from_static_string ("UnreadMailCount");
            prop_quarks[PROP_UNREAD_MAILS] =
                g_quark_from_static_string ("UnreadMails");
            prop_quarks[PROP_MAIL_ADDRESS] =
                g_quark_from_static_string ("MailAddress");
        }

    DEBUG ("MailNotification get property %s", g_quark_to_string (name));

    if (name == prop_quarks[PROP_MAIL_NOTIFICATION_FLAGS])
        g_value_set_uint (value,
            HAZE_MAIL_NOTIFICATION_FLAG_EMITS_MAILS_RECEIVED
            | HAZE_MAIL_NOTIFICATION_FLAG_SUPPORTS_REQUEST_MAIL_URL);
    else if (name == prop_quarks[PROP_UNREAD_MAIL_COUNT])
        g_value_set_uint (value, 0);
    else if (name == prop_quarks[PROP_UNREAD_MAILS])
        g_value_set_boxed (value, &empty_array);
    else if (name == prop_quarks[PROP_MAIL_ADDRESS])
        g_value_take_string (value, _get_email (conn->account));
    else
        g_assert (!"Unknown mail notification property, please file a bug.");
}


static inline const gchar *
_account_name (PurpleConnection *pc)
{
    return purple_account_get_username (purple_connection_get_account (pc));
}


gpointer
haze_connection_mail_notify_email (PurpleConnection *pc,
        const char *subject,
        const char *from,
        const char *to,
        const char *url)
{
    return haze_connection_mail_notify_emails (pc, 1, TRUE,
            &subject, &from, &to, &url);
}


static GPtrArray *
wrap_mail_address (const char *name_str, const char *addr_str)
{
    GPtrArray *addr_array;
    GType addr_type = HAZE_STRUCT_TYPE_MAIL_ADDRESS;
    GValue addr = {0};

    addr_array = g_ptr_array_new ();

    g_value_init (&addr, addr_type);
    g_value_set_static_boxed (&addr,
            dbus_g_type_specialized_construct (addr_type));

    dbus_g_type_struct_set (&addr,
            0, name_str,
            1, addr_str,
            G_MAXUINT);

    g_ptr_array_add (addr_array, g_value_get_boxed(&addr));
    g_value_unset (&addr);
    return addr_array;
}


gpointer
haze_connection_mail_notify_emails (PurpleConnection *pc,
        size_t count,
        gboolean detailed,
        const char **subjects,
        const char **froms,
        const char **tos,
        const char **urls)
{
    GPtrArray *mails;
    HazeSvcConnectionInterfaceMailNotification *conn =
        HAZE_SVC_CONNECTION_INTERFACE_MAIL_NOTIFICATION (
            ACCOUNT_GET_TP_BASE_CONNECTION (
                purple_connection_get_account (pc)));

    DEBUG ("[%s] %" G_GSIZE_FORMAT " new emails", _account_name (pc), count);

    /* FIXME: Count is broken in libpurple, until it's fixed, just ignore
     * messages wihout details. */
    if (!detailed)
        return NULL;

    /* For consitency, ignore messages without subject, from or url */
    if (subjects == NULL || froms == NULL || urls == NULL)
        return NULL;

    mails = g_ptr_array_new_with_free_func (
            (GDestroyNotify)g_hash_table_destroy);

    for (; count; count--)
        {
            const char *from, *to, *subject, *url;

            from = *froms;
            to = *tos;
            subject = *subjects;
            url = *urls;

            DEBUG ("[%s] from: %s; to: %s; subject: %s; url: %s",
                    _account_name (pc), from, to, subject, url);

            /* Filter out any aberations */
            if (from && to && subject && url)
                {
                    GType addr_list_type = HAZE_ARRAY_TYPE_MAIL_ADDRESS_LIST;
                    GPtrArray *senders, *recipients;
                    GHashTable *mail;

                    if (g_utf8_strchr(from, -1, '@'))
                        senders = wrap_mail_address ("", from);
                    else
                        senders =  wrap_mail_address (from, "");

                    if (g_utf8_strchr(to, -1, '@'))
                        recipients = wrap_mail_address ("", to);
                    else
                        recipients = wrap_mail_address (to, "");

                    mail = tp_asv_new (
                            "url-data", G_TYPE_STRING, url,
                            "senders", addr_list_type, senders,
                            "to-address", addr_list_type, recipients,
                            "subject", G_TYPE_STRING, subject,
                            NULL);
                    g_ptr_array_add (mails, mail);

                    g_ptr_array_unref (senders);
                    g_ptr_array_unref (recipients);

                    froms++;
                    tos++;
                    subject++;

                    /* Some protocols only set one URL (the inbox URL), so
                     * reuse previous URL if next one does not exist */
                    if (count > 1 && urls[1] != NULL)
                        urls++;
                }
        }

    haze_svc_connection_interface_mail_notification_emit_mails_received (
            conn, mails);
    g_ptr_array_unref (mails);

    return NULL;
}

