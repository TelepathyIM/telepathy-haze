#ifndef __HAZE_CONTACT_LIST_CHANNEL_H__
#define __HAZE_CONTACT_LIST_CHANNEL_H__
/*
 * contact-list-channel.h - HazeContactListChannel header
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

#include <telepathy-glib/group-mixin.h>
#include <telepathy-glib/dbus-properties-mixin.h>

#include <libpurple/account.h>

G_BEGIN_DECLS

typedef struct _HazeContactListChannel HazeContactListChannel;
typedef struct _HazeContactListChannelClass HazeContactListChannelClass;

struct _HazeContactListChannelClass {
    GObjectClass parent_class;
    TpGroupMixinClass group_class;
    TpDBusPropertiesMixinClass properties_class;
};

struct _HazeContactListChannel {
    GObject parent;

    TpGroupMixin group;

    gpointer priv;
};

GType haze_contact_list_channel_get_type (void);

gpointer haze_request_authorize (PurpleAccount *account,
    const char *remote_user, const char *id, const char *alias,
    const char *message, gboolean on_list,
    PurpleAccountRequestAuthorizationCb authorize_cb,
    PurpleAccountRequestAuthorizationCb deny_cb, void *user_data);

void haze_close_account_request (gpointer request_data_);

/* TYPE MACROS */
#define HAZE_TYPE_CONTACT_LIST_CHANNEL \
  (haze_contact_list_channel_get_type ())
#define HAZE_CONTACT_LIST_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), HAZE_TYPE_CONTACT_LIST_CHANNEL, \
                              HazeContactListChannel))
#define HAZE_CONTACT_LIST_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), HAZE_TYPE_CONTACT_LIST_CHANNEL, \
                           HazeContactListChannelClass))
#define HAZE_IS_CONTACT_LIST_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), HAZE_TYPE_CONTACT_LIST_CHANNEL))
#define HAZE_IS_CONTACT_LIST_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), HAZE_TYPE_CONTACT_LIST_CHANNEL))
#define HAZE_CONTACT_LIST_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), HAZE_CONTACT_LIST_CHANNEL, \
                              HazeContactListChannelClass))

G_END_DECLS

#endif /* #ifndef __HAZE_CONTACT_LIST_CHANNEL_H__*/
