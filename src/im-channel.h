#ifndef __HAZE_IM_CHANNEL_H__
#define __HAZE_IM_CHANNEL_H__

#include <glib-object.h>
#include <telepathy-glib/text-mixin.h>

G_BEGIN_DECLS

typedef struct _HazeIMChannel HazeIMChannel;
typedef struct _HazeIMChannelClass HazeIMChannelClass;

struct _HazeIMChannelClass {
    GObjectClass parent_class;

    TpTextMixinClass text_class;
};

struct _HazeIMChannel {
    GObject parent;

    TpTextMixin text;

    gpointer priv;
};

GType haze_im_channel_get_type (void);

/* TYPE MACROS */
#define HAZE_TYPE_IM_CHANNEL \
  (haze_im_channel_get_type ())
#define HAZE_IM_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), HAZE_TYPE_IM_CHANNEL, \
                              HazeIMChannel))
#define HAZE_IM_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), HAZE_TYPE_IM_CHANNEL, \
                           HazeIMChannelClass))
#define HAZE_IS_IM_CHANNEL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), HAZE_TYPE_IM_CHANNEL))
#define HAZE_IS_IM_CHANNEL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), HAZE_TYPE_IM_CHANNEL))
#define HAZE_IM_CHANNEL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), HAZE_TYPE_IM_CHANNEL, \
                              HazeIMChannelClass))

G_END_DECLS

#endif /* #ifndef __HAZE_IM_CHANNEL_H__*/
