/*
 * haze-connection-mail.h - MailNotification interface header of HazeConnection
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

#ifndef __HAZE_CONNECTION_MAIL_H__
#define __HAZE_CONNECTION_MAIL_H__

#include <glib-object.h>

G_BEGIN_DECLS

void haze_connection_mail_iface_init (gpointer g_iface, gpointer iface_data);
void haze_connection_mail_class_init (GObjectClass *object_class);
void haze_connection_mail_init (GObject *object);
void haze_connection_mail_properties_getter (GObject *object, GQuark interface,
        GQuark name, GValue *value, gpointer getter_data);

gpointer haze_connection_mail_notify_email (PurpleConnection *gc, 
        const char *subject, const char *from, const char *to, const char *url);
gpointer haze_connection_mail_notify_emails (PurpleConnection *gc, 
        size_t count, gboolean detailed, const char **subjects, 
        const char **froms, const char **tos, const char **urls); 

G_END_DECLS

#endif /* __HAZE_CONNECTION_MAIL_H__ */

