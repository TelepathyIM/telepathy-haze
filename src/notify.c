/*
 * notify.c - stub implementation of the libpurple notify API.
 * Copyright (C) 2007 Collabora Ltd.
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

#include "notify.h"
#include "debug.h"

static const gchar *
_account_name (PurpleConnection *gc)
{
    return purple_account_get_username (purple_connection_get_account (gc));
}

static const gchar *
_stringify_notify_message_type (PurpleNotifyMsgType type)
{
    switch (type)
    {
        case PURPLE_NOTIFY_MSG_ERROR:
            return "error";
        case PURPLE_NOTIFY_MSG_WARNING:
            return "warning";
        case PURPLE_NOTIFY_MSG_INFO:
            return "info";
        default:
            return "(invalid PurpleNotifyMsgType)";
    }
}

static gpointer
haze_notify_message (PurpleNotifyMsgType type,
                     const char *title,
                     const char *primary,
                     const char *secondary)
{
    DEBUG ("%s: %s", _stringify_notify_message_type (type), title);
    DEBUG ("%s", primary);
    DEBUG ("%s", secondary);
    return NULL;
}

static gpointer
haze_notify_email (PurpleConnection *gc,
                   const char *subject,
                   const char *from,
                   const char *to,
                   const char *url)
{
    DEBUG ("[%s] from: %s; to: %s; subject: %s; url: %s", _account_name (gc),
        from, to, subject, url);
    return NULL;
}

static gpointer
haze_notify_emails (PurpleConnection *gc,
                    size_t count,
                    gboolean detailed,
                    const char **subjects,
                    const char **froms,
                    const char **tos,
                    const char **urls)
{
    DEBUG ("[%s] %" G_GSIZE_FORMAT " new emails", _account_name (gc), count);
    return NULL;
}

static gpointer
haze_notify_formatted (const char *title,
                       const char *primary,
                       const char *secondary,
                       const char *text)
{
    DEBUG ("%s", title);
    DEBUG ("%s", primary);
    DEBUG ("%s", secondary);
    DEBUG ("%s", text);
    return NULL;
}

static gpointer
haze_notify_uri (const char *uri)
{
    DEBUG ("%s", uri);
    return NULL;
}

static gpointer
haze_notify_userinfo (PurpleConnection *gc,
                      const char *who,
                      PurpleNotifyUserInfo *user_info)
{
    DEBUG ("[%s] %s", _account_name (gc), who);
    return NULL;
}

static PurpleNotifyUiOps notify_ui_ops =
{
    .notify_message = haze_notify_message,
    .notify_email = haze_notify_email,
    .notify_emails = haze_notify_emails,
    .notify_formatted = haze_notify_formatted,
    .notify_userinfo = haze_notify_userinfo,
    .notify_uri = haze_notify_uri,
    .notify_searchresults = NULL,
    .notify_searchresults_new_rows = NULL
};

PurpleNotifyUiOps *
haze_notify_get_ui_ops ()
{
    return &notify_ui_ops;
}
