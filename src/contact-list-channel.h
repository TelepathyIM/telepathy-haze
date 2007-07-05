#ifndef __HAZE_CONTACT_LIST_CHANNEL_H__
#define __HAZE_CONTACT_LIST_CHANNEL_H__

#include <glib-object.h>
#include <telepathy-glib/group-mixin.h>

G_BEGIN_DECLS

typedef struct _HazeContactListChannel HazeContactListChannel;
typedef struct _HazeContactListChannelClass HazeContactListChannelClass;

struct _HazeContactListChannelClass {
    GObjectClass parent_class;
    TpGroupMixinClass group_class;
};

struct _HazeContactListChannel {
    GObject parent;

    TpGroupMixin group;

    gpointer priv;
};

GType haze_contact_list_channel_get_type (void);

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
