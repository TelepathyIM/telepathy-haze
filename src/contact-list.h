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

#include "contact-list-channel.h"

G_BEGIN_DECLS

typedef struct _HazeContactList HazeContactList;
typedef struct _HazeContactListClass HazeContactListClass;
typedef struct _HazeContactListPrivate HazeContactListPrivate;

struct _HazeContactListClass {
    GObjectClass parent_class;
};

struct _HazeContactList {
    GObject parent;
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

HazeContactListChannel *haze_contact_list_get_channel (HazeContactList *self,
    guint handle_type, TpHandle handle, gpointer request_token,
    gboolean *created);

#endif /* #ifndef __HAZE_CONTACT_LIST_H__*/
