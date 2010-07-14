/*
 * Haze Protocol object
 *
 * Copyright © 2007 Will Thompson
 * Copyright © 2007-2010 Collabora Ltd.
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

#ifndef __HAZE_PROTOCOL_H__
#define __HAZE_PROTOCOL_H__

#include <telepathy-glib/base-protocol.h>

#include <libpurple/prpl.h>

G_BEGIN_DECLS

typedef struct _HazeProtocol HazeProtocol;
typedef struct _HazeProtocolClass HazeProtocolClass;
typedef struct _HazeProtocolPrivate HazeProtocolPrivate;

GType haze_protocol_get_type (void) G_GNUC_CONST;

#define HAZE_TYPE_PROTOCOL \
  (haze_protocol_get_type ())
#define HAZE_PROTOCOL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), HAZE_TYPE_PROTOCOL, \
                               HazeProtocol))
#define HAZE_PROTOCOL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), HAZE_TYPE_PROTOCOL, \
                            HazeProtocolClass))
#define HAZE_IS_PROTOCOL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), HAZE_TYPE_PROTOCOL))
#define HAZE_IS_PROTOCOL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), HAZE_TYPE_PROTOCOL))
#define HAZE_PROTOCOL_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), HAZE_TYPE_PROTOCOL, \
                              HazeProtocolClass))

struct _HazeProtocolClass
{
  TpBaseProtocolClass parent;
};

struct _HazeProtocol
{
  TpBaseProtocol parent;
  HazeProtocolPrivate *priv;
};

GList *haze_protocol_build_list (void);

G_END_DECLS

#endif
