/*
 * debug.c - haze's libpurple debug machinery
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

#include <string.h>

#include <libpurple/debug.h>

static char *debug_level_names[] =
{
    "all",
    "misc",
    "info",
    "warning",
    "error",
    "fatal"
};

static void
haze_debug_print (PurpleDebugLevel level,
                  const char *category,
                  const char *arg_s)
{
    char *argh = g_strchomp (g_strdup (arg_s));
    const char *level_name = debug_level_names[level];
    switch (level)
    {
        case PURPLE_DEBUG_WARNING:
            g_warning ("%s: %s", category, argh);
            break;
        case PURPLE_DEBUG_FATAL:
            /* g_critical doesn't cause an abort() in haze, so libpurple will
             * still get to do the honours of blowing us out of the water.
             */
            g_critical ("[%s] %s: %s", level_name, category, argh);
            break;
        case PURPLE_DEBUG_ERROR:
        case PURPLE_DEBUG_MISC:
        case PURPLE_DEBUG_INFO:
        default:
            g_message ("[%s] %s: %s", level_name, category, argh);
            break;
    }
    g_free(argh);
}

static gboolean
haze_debug_is_enabled (PurpleDebugLevel level,
                       const char *category)
{
    if (level == PURPLE_DEBUG_MISC)
        return FALSE;
    /* oscar and yahoo, among others, supply a NULL category for some of their
     * output.  "yay"
     */
    if (!category)
        return FALSE;
    /* The Jabber prpl produces an unreasonable volume of debug output, so
     * let's suppress it.
     */
    if (!strcmp (category, "jabber"))
        return FALSE;
    if (!strcmp (category, "dns") ||
        !strcmp (category, "dnsquery") ||
        !strcmp (category, "proxy") ||
        !strcmp (category, "gnutls"))
        return FALSE;
    return TRUE;
}

static PurpleDebugUiOps haze_debug_uiops =
{
    haze_debug_print,
    haze_debug_is_enabled,
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