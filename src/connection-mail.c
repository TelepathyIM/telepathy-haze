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

#include <telepathy-glib/svc-connection.h>
#include <telepathy-glib/interfaces.h>

#include "connection.h"
#include "connection-mail.h"
#include "debug.h"

enum
{
    PROP_CAPABILITIES,
    PROP_UNREAD_MAIL_COUNT,
    PROP_UNREAD_MAILS,
    NUM_OF_PROP,
};

static GPtrArray empty_array = { 0 };


static void
haze_connection_mail_subscribe (HazeSvcConnectionInterfaceMailNotification *iface,
        DBusGMethodInvocation *context)
{
    /* TODO */
}


static void
haze_connection_mail_unsubscribe (HazeSvcConnectionInterfaceMailNotification *iface,
        DBusGMethodInvocation *context)
{
    /* TODO */
}


static void
haze_connection_mail_request_inbox_url (
        HazeSvcConnectionInterfaceMailNotification *iface,
        DBusGMethodInvocation *context)
{
    /* TODO */
}


static void
haze_connection_mail_request_mail_url (
        HazeSvcConnectionInterfaceMailNotification *iface,
        const gchar *in_id,
        const gchar *in_url_data,
        DBusGMethodInvocation *context)
{
    /* TODO */
}


void
haze_connection_mail_init (GObject *conn)
{
    /* TODO */
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


void
haze_connection_mail_properties_getter (GObject *object,
        GQuark interface,
        GQuark name,
        GValue *value,
        gpointer getter_data)
{
    static GQuark prop_quarks[NUM_OF_PROP] = {0};

    if (G_UNLIKELY (prop_quarks[0] == 0))
        {
            prop_quarks[PROP_CAPABILITIES] = 
                g_quark_from_static_string ("Capabilities");
            prop_quarks[PROP_UNREAD_MAIL_COUNT] = 
                g_quark_from_static_string ("UnreadMailCount");
            prop_quarks[PROP_UNREAD_MAILS] = 
                g_quark_from_static_string ("UnreadMails");
        }

    DEBUG ("MailNotification get property %s", g_quark_to_string (name));

    if (name == prop_quarks[PROP_CAPABILITIES])
        g_value_set_uint (value, 0);
    else if (name == prop_quarks[PROP_UNREAD_MAIL_COUNT])
        g_value_set_uint (value, 0);
    else if (name == prop_quarks[PROP_UNREAD_MAILS])
        g_value_set_boxed (value, &empty_array);
    else
        g_assert (!"Unknown mail notification property, please file a bug.");
}
