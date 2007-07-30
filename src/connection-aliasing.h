#ifndef __HAZE_CONNECTION_ALIASING_H__
#define __HAZE_CONNECTION_ALIASING_H__

#include <glib-object.h>

void
haze_connection_aliasing_iface_init (gpointer g_iface,
                                     gpointer iface_data);

void
haze_connection_aliasing_class_init (GObjectClass *object_class);

#endif
