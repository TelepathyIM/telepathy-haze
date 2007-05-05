#ifndef __HAZE_CONNECTION_MANAGER_H__
#define __HAZE_CONNECTION_MANAGER_H__

#include <glib-object.h>
#include <telepathy-glib/base-connection-manager.h>

G_BEGIN_DECLS

typedef struct _HazeConnectionManager HazeConnectionManager;
typedef struct _HazeConnectionManagerClass HazeConnectionManagerClass;

struct _HazeConnectionManagerClass {
    TpBaseConnectionManagerClass parent_class;
};

struct _HazeConnectionManager {
    TpBaseConnectionManager parent;
};

GType haze_connection_manager_get_type (void);

/* TYPE MACROS */
#define HAZE_TYPE_CONNECTION_MANAGER \
  (haze_connection_manager_get_type ())
#define HAZE_CONNECTION_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), HAZE_TYPE_CONNECTION_MANAGER, \
                              HazeConnectionManager))
#define HAZE_CONNECTION_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass), HAZE_TYPE_CONNECTION_MANAGER, \
                           HazeConnectionManagerClass))
#define HAZE_IS_CONNECTION_MANAGER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj), HAZE_TYPE_CONNECTION_MANAGER))
#define HAZE_IS_CONNECTION_MANAGER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass), HAZE_TYPE_CONNECTION_MANAGER))
#define HAZE_CONNECTION_MANAGER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), HAZE_TYPE_CONNECTION_MANAGER, \
                              HazeConnectionManagerClass))

G_END_DECLS

#endif /* #ifndef __HAZE_CONNECTION_MANAGER_H__*/
