#ifndef __HAZE_CONTACT_LIST_H__
#define __HAZE_CONTACT_LIST_H__

#include <glib-object.h>

G_BEGIN_DECLS

typedef struct _HazeContactList HazeContactList;
typedef struct _HazeContactListClass HazeContactListClass;

struct _HazeContactListClass {
    GObjectClass parent_class;
};

struct _HazeContactList {
    GObject parent;
    gpointer priv;
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

#endif /* #ifndef __HAZE_CONTACT_LIST_H__*/
