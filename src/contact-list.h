#ifndef __HAZE_CONTACT_LIST_H__
#define __HAZE_CONTACT_LIST_H__
/*
 * contact-list.h - HazeContactList header
 * Copyright (C) 2007 Will Thompson
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

#include <telepathy-glib/base-contact-list.h>

G_BEGIN_DECLS

typedef struct _HazeContactList HazeContactList;
typedef struct _HazeContactListClass HazeContactListClass;
typedef struct _HazeContactListPrivate HazeContactListPrivate;

struct _HazeContactListClass {
    TpBaseContactListClass parent_class;
};

struct _HazeContactList {
    TpBaseContactList parent;
    HazeContactListPrivate *priv;
};

GType haze_contact_list_get_type (void);

/* TYPE MACROS */
#define HAZE_TYPE_CONTACT_LIST \
  (haze_contact_list_get_type ())
#define HAZE_CONTACT_LIST(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), HAZE_TYPE_CONTACT_LIST, \
                              HazeContactList))
#define HAZE_CONTACT_LIST_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), HAZE_TYPE_CONTACT_LIST, \
                           HazeContactListClass))
#define HAZE_IS_CONTACT_LIST(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), HAZE_TYPE_CONTACT_LIST))
#define HAZE_IS_CONTACT_LIST_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), HAZE_TYPE_CONTACT_LIST))
#define HAZE_CONTACT_LIST_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), HAZE_CONTACT_LIST, \
                              HazeContactListClass))

G_END_DECLS

gpointer haze_request_authorize (PurpleAccount *account,
    const char *remote_user, const char *id, const char *alias,
    const char *message, gboolean on_list,
    PurpleAccountRequestAuthorizationCb authorize_cb,
    PurpleAccountRequestAuthorizationCb deny_cb, void *user_data);
void haze_close_account_request (gpointer request_data_);

void haze_contact_list_accept_publish_request (HazeContactList *self,
    TpHandle handle);
void haze_contact_list_reject_publish_request (HazeContactList *self,
    TpHandle handle);

void haze_contact_list_request_subscription (HazeContactList *self,
    TpHandle handle, const gchar *message);
void haze_contact_list_remove_contact (HazeContactList *self,
    TpHandle handle);

void haze_contact_list_add_to_group (HazeContactList *self,
    const gchar *group_name, TpHandle handle);
gboolean haze_contact_list_remove_from_group (HazeContactList *self,
    const gchar *group_name, TpHandle handle, GError **error);

PurplePrivacyUiOps *haze_get_privacy_ui_ops (void);

#endif /* #ifndef __HAZE_CONTACT_LIST_H__*/
