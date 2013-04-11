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

#include "config.h"

#include <glib-object.h>

#include <libpurple/account.h>
#include <libpurple/conversation.h>

#include "debug.h"
#include "request.h"
#include "connection.h"

#ifdef ENABLE_LEAKY_REQUEST_STUBS
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
#endif

struct fields_data {
    PurpleAccount *account;
    PurpleRequestFields *fields;
    PurpleRequestField *password;
    PurpleRequestFieldsCb ok_cb;
    PurpleRequestFieldsCb cancel_cb;
    void *user_data;
};

static void
haze_close_request (PurpleRequestType type,
                    void *ui_handle)
{
    struct fields_data *fd = ui_handle;

    haze_connection_cancel_password_request (fd->account);
    purple_request_fields_destroy (fd->fields);
    g_slice_free (struct fields_data, fd);
}

void
haze_request_password_cb (gpointer user_data,
                          const gchar *password)
{
    struct fields_data *fd = user_data;

    if (password)
      {
        purple_request_field_string_set_value (fd->password, password);
        if (fd->ok_cb)
          {
            (fd->ok_cb) (fd->user_data, fd->fields);
          }
      }
    else
      {
        if (fd->cancel_cb)
          {
            (fd->cancel_cb) (fd->user_data, fd->fields);
          }
      }

    purple_request_close (PURPLE_REQUEST_FIELDS, fd);
}

static gboolean
haze_request_fields_destroy (gpointer user_data)
{
    struct fields_data *fd = user_data;

    if (fd->cancel_cb)
      {
        (fd->cancel_cb) (fd->user_data, fd->fields);
      }

    purple_request_close (PURPLE_REQUEST_FIELDS, user_data);

    return FALSE;
}

/*
 * We must support purple_account_request_password() which boils down
 * to purple_request_fields() with certain parameters. I'm not sure
 * if this the best way of doing this, but it works.
 */
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
    struct fields_data *fd = g_slice_new0 (struct fields_data);

    /* it is our responsibility to destroy this data */
    fd->account   = account;
    fd->fields    = fields;
    fd->cancel_cb = (PurpleRequestFieldsCb) cancel_cb;
    fd->user_data = user_data;

    if (purple_request_fields_exists (fields, "password") &&
        purple_request_fields_exists (fields, "remember"))
      {

        DEBUG ("triggering password request");

        fd->password = purple_request_fields_get_field (fields, "password");
        fd->ok_cb    = (PurpleRequestFieldsCb) ok_cb;

        haze_connection_request_password (account, fd);

      }
    else
      {
        DEBUG ("ignoring request:");
        DEBUG ("    title: %s", (title ? title : "(null)"));
        DEBUG ("    primary: %s", (primary ? primary : "(null)"));
        DEBUG ("    secondary: %s", (secondary ? secondary : "(null)"));

        /* Avoid leaking of "fields" and "user_data" */
        g_idle_add (haze_request_fields_destroy, fd);
      }

    return fd;
}

#ifdef ENABLE_LEAKY_REQUEST_STUBS
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
#endif

static PurpleRequestUiOps request_uiops =
{
#ifdef ENABLE_LEAKY_REQUEST_STUBS
    .request_input = haze_request_input,
    .request_choice = haze_request_choice,
    .request_action = haze_request_action,
    .request_file = haze_request_file,
    .request_folder = haze_request_folder,
#endif
    .request_fields = haze_request_fields,
    .close_request  = haze_close_request
};

PurpleRequestUiOps *
haze_request_get_ui_ops ()
{
    return &request_uiops;
}
