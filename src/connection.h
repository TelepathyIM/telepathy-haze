#ifndef __HAZE_CONNECTION_H__
#define __HAZE_CONNECTION_H__


#include <glib-object.h>
#include <telepathy-glib/base-connection.h>
#include <prpl.h>

G_BEGIN_DECLS

typedef struct _HazeConnection HazeConnection;
typedef struct _HazeConnectionClass HazeConnectionClass;

struct _HazeConnectionClass {
    TpBaseConnectionClass parent_class;
};

struct _HazeConnection {
    TpBaseConnection parent;
    PurpleAccount *account;
    gpointer priv;
};

GType haze_connection_get_type (void);

/* TYPE MACROS */
#define HAZE_TYPE_CONNECTION \
  (haze_connection_get_type ())
#define HAZE_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), HAZE_TYPE_CONNECTION, \
                              HazeConnection))
#define HAZE_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), HAZE_TYPE_CONNECTION, \
                           HazeConnectionClass))
#define HAZE_IS_CONNECTION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), HAZE_TYPE_CONNECTION))
#define HAZE_IS_CONNECTION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), HAZE_TYPE_CONNECTION))
#define HAZE_CONNECTION_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), HAZE_TYPE_CONNECTION, \
                              HazeConnectionClass))

G_END_DECLS

#endif /* #ifndef __HAZE_CONNECTION_H__*/
