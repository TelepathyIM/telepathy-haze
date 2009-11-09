/*
 * debug.c - haze's debug machinery for itself and libpurple
 * Copyright (C) 2007 Collabora Ltd.
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

#include "debug.h"

#include <string.h>
#include <stdarg.h>

#include <libpurple/debug.h>
#include <telepathy-glib/debug.h>
#include <telepathy-glib/debug-sender.h>


typedef enum
{
    HAZE_DEBUG_HAZE   = 1 << 0,
    HAZE_DEBUG_PURPLE = 1 << 1,
} HazeDebugFlags;


static GDebugKey keys[] =
{
    { "haze",   HAZE_DEBUG_HAZE },
    { "purple", HAZE_DEBUG_PURPLE },
};


static HazeDebugFlags flags = 0;


void
haze_debug_set_flags_from_env ()
{
    const gchar *env = g_getenv ("HAZE_DEBUG");

    if (env)
    {
       flags |= g_parse_debug_string (env, keys, 2);
    }

    tp_debug_set_flags (env);

    if (g_getenv ("HAZE_PERSIST"))
        tp_debug_set_persistent (TRUE);

    tp_debug_divert_messages (g_getenv ("HAZE_LOGFILE"));
}


static GLogLevelFlags debug_level_map[] =
{
    G_LOG_LEVEL_DEBUG,    /* PURPLE_DEBUG_ALL */
    G_LOG_LEVEL_INFO,     /* PURPLE_DEBUG_MISC */
    G_LOG_LEVEL_MESSAGE,  /* PURPLE_DEBUG_INFO */
    G_LOG_LEVEL_WARNING,  /* PURPLE_DEBUG_WARNING */
    G_LOG_LEVEL_CRITICAL, /* PURPLE_DEBUG_ERROR */
    G_LOG_LEVEL_ERROR,    /* PURPLE_DEBUG_CRITICAL */
};

static void
log_to_debug_sender (const gchar *domain,
                     GLogLevelFlags level,
                     const gchar *message)
{
    TpDebugSender *dbg;
    GTimeVal now;

    dbg = tp_debug_sender_dup ();

    g_get_current_time (&now);

    tp_debug_sender_add_message (dbg, &now, domain, level, message);

    g_object_unref (dbg);
}

static void
haze_debug_print (PurpleDebugLevel level,
                  const char *category,
                  const char *arg_s)
{
    gchar *argh = g_strchomp (g_strdup (arg_s));
    gchar *domain = g_strdup_printf ("purple/%s", category);
    GLogLevelFlags log_level = debug_level_map[level];

    log_to_debug_sender (domain, log_level, argh);

    if (flags & HAZE_DEBUG_PURPLE)
        g_log (domain, log_level, "%s", argh);

    g_free (domain);
    g_free(argh);
}

static PurpleDebugUiOps haze_debug_uiops =
{
    haze_debug_print,
    NULL,
    /* padding */
    NULL,
    NULL,
    NULL,
    NULL,
};

void
haze_debug_init(void)
{
    /* Disable spewing debug information directly to the terminal.  The debug
     * uiops deal with it.
     */
    purple_debug_set_enabled(FALSE);

    purple_debug_set_ui_ops(&haze_debug_uiops);
}

void
haze_debug (const gchar *format,
            ...)
{
    gchar *message;
    va_list args;

    va_start (args, format);
    message = g_strdup_vprintf (format, args);
    va_end (args);

    log_to_debug_sender (G_LOG_DOMAIN "/haze", G_LOG_LEVEL_DEBUG, message);

    if (flags & HAZE_DEBUG_HAZE)
        g_log (G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, "%s", message);

    g_free (message);
}

