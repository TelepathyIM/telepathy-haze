#ifndef __HAZE_IM_CHANNEL_FACTORY_H__
#define __HAZE_IM_CHANNEL_FACTORY_H__
/*
 * im-channel-factory.h - HazeImChannelFactory header
 * Copyright (C) 2007 Will Thompson
 * Copyright (C) 2007-2008 Collabora Ltd.
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

#include <libpurple/conversation.h>

G_BEGIN_DECLS

#define HAZE_TYPE_IM_CHANNEL_FACTORY \
    (haze_im_channel_factory_get_type())
#define HAZE_IM_CHANNEL_FACTORY(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), HAZE_TYPE_IM_CHANNEL_FACTORY, \
                                HazeImChannelFactory))
#define HAZE_IM_CHANNEL_FACTORY_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), HAZE_TYPE_IM_CHANNEL_FACTORY,GObject))
#define HAZE_IS_IM_CHANNEL_FACTORY(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), HAZE_TYPE_IM_CHANNEL_FACTORY))
#define HAZE_IS_IM_CHANNEL_FACTORY_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), HAZE_TYPE_IM_CHANNEL_FACTORY))
#define HAZE_IM_CHANNEL_FACTORY_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), HAZE_TYPE_IM_CHANNEL_FACTORY, \
                               HazeImChannelFactoryClass))

typedef struct _HazeImChannelFactory      HazeImChannelFactory;
typedef struct _HazeImChannelFactoryClass HazeImChannelFactoryClass;
typedef struct _HazeImChannelFactoryPrivate HazeImChannelFactoryPrivate;

struct _HazeImChannelFactory {
    GObject parent;
    HazeImChannelFactoryPrivate *priv;
};

struct _HazeImChannelFactoryClass {
    GObjectClass parent_class;
};

GType haze_im_channel_factory_get_type (void) G_GNUC_CONST;

PurpleConversationUiOps *haze_get_conv_ui_ops (void);

G_END_DECLS

#endif /* __HAZE_IM_CHANNEL_FACTORY_H__ */

