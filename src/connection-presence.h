#ifndef __HAZE_CONNECTION_PRESENCE_H__
#define __HAZE_CONNECTION_PRESENCE_H__

#include <glib-object.h>

#include "connection.h"

void haze_connection_presence_class_init (GObjectClass *object_class);
void haze_connection_presence_init (HazeConnection *self);

#endif
