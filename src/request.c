/*
 * request.c - stub implementation of the libpurple request API.
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

#include <glib-object.h>

#include <libpurple/account.h>
#include <libpurple/conversation.h>

#include "debug.h"
#include "request.h"

static gpointer
haze_request_input (const char *title,
                    const char *primary,
                    const char *secondary,
                    const char *default_value,
                    gboolean multiline,
                    gboolean masked,
                    gchar *hint,
                    const char *ok_text,
                    GCallback ok_cb,
                    const char *cancel_text,
                    GCallback cancel_cb,
                    PurpleAccount *account,
                    const char *who,
                    PurpleConversation *conv,
                    void *user_data)
{
    DEBUG ("ignoring request:");
    DEBUG ("    title: %s", (title ? title : "(null)"));
    DEBUG ("    primary: %s", (primary ? primary : "(null)"));
    DEBUG ("    secondary: %s", (secondary ? secondary : "(null)"));
    DEBUG ("    default_value: %s", default_value ? default_value : "(null)");

    return NULL;
}

static gpointer
haze_request_choice (const char *title,
                     const char *primary,
                     const char *secondary,
                     int default_value,
                     const char *ok_text,
                     GCallback ok_cb,
                     const char *cancel_text,
                     GCallback cancel_cb,
                     PurpleAccount *account,
                     const char *who,
                     PurpleConversation *conv,
                     void *user_data,
                     va_list choices)
{
    DEBUG ("ignoring request:");
    DEBUG ("    title: %s", (title ? title : "(null)"));
    DEBUG ("    primary: %s", (primary ? primary : "(null)"));
    DEBUG ("    secondary: %s", (secondary ? secondary : "(null)"));
    DEBUG ("    default_value: %i", default_value);

    return NULL;
}

static gpointer
haze_request_action (const char *title,
                     const char *primary,
                     const char *secondary,
                     int default_action,
                     PurpleAccount *account,
                     const char *who,
                     PurpleConversation *conv,
                     void *user_data,
                     size_t action_count,
                     va_list actions)
{
    DEBUG ("ignoring request:");
    DEBUG ("    title: %s", (title ? title : "(null)"));
    DEBUG ("    primary: %s", (primary ? primary : "(null)"));
    DEBUG ("    secondary: %s", (secondary ? secondary : "(null)"));

    return NULL;
}

static gpointer
haze_request_fields (const char *title,
                     const char *primary,
                     const char *secondary,
                     PurpleRequestFields *fields,
                     const char *ok_text,
                     GCallback ok_cb,
                     const char *cancel_text,
                     GCallback cancel_cb,
                     PurpleAccount *account,
                     const char *who,
                     PurpleConversation *conv,
                     void *user_data)
{
    DEBUG ("ignoring request:");
    DEBUG ("    title: %s", (title ? title : "(null)"));
    DEBUG ("    primary: %s", (primary ? primary : "(null)"));
    DEBUG ("    secondary: %s", (secondary ? secondary : "(null)"));

    return NULL;
}

static gpointer
haze_request_file (const char *title,
                   const char *filename,
                   gboolean savedialog,
                   GCallback ok_cb,
                   GCallback cancel_cb,
                   PurpleAccount *account,
                   const char *who,
                   PurpleConversation *conv,
                   void *user_data)
{
    DEBUG ("ignoring request:");
    DEBUG ("    title: %s", (title ? title : "(null)"));
    DEBUG ("    filename: %s", (filename ? filename : "(null)"));

    return NULL;
}

static gpointer
haze_request_folder (const char *title,
                     const char *dirname,
                     GCallback ok_cb,
                     GCallback cancel_cb,
                     PurpleAccount *account,
                     const char *who,
                     PurpleConversation *conv,
                     void *user_data)
{
    DEBUG ("ignoring request:");
    DEBUG ("    title: %s", (title ? title : "(null)"));
    DEBUG ("    dirname: %s", (dirname ? dirname : "(null)"));

    return NULL;
}


/*
	void (*close_request)(PurpleRequestType type, void *ui_handle);
*/

static PurpleRequestUiOps request_uiops =
{
    .request_input = haze_request_input,
    .request_choice = haze_request_choice,
    .request_action = haze_request_action,
    .request_fields = haze_request_fields,
    .request_file = haze_request_file,
    .request_folder = haze_request_folder
};

PurpleRequestUiOps *
haze_request_get_ui_ops ()
{
    return &request_uiops;
}
