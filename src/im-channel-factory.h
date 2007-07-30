#ifndef __HAZE_IM_CHANNEL_FACTORY_H__
#define __HAZE_IM_CHANNEL_FACTORY_H__

#include <glib-object.h>

#include <conversation.h>

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

struct _HazeImChannelFactory {
    GObject parent;
};

struct _HazeImChannelFactoryClass {
    GObjectClass parent_class;
};

GType        haze_im_channel_factory_get_type    (void) G_GNUC_CONST;

PurpleConversationUiOps *haze_get_conv_ui_ops (void);

G_END_DECLS

#endif /* __HAZE_IM_CHANNEL_FACTORY_H__ */

