/*
 * main.c - entry point and libpurple boilerplate for telepathy-haze
 * Copyright (C) 2007 Will Thompson
 * Copyright (C) 2007 Collabora Ltd.
 * Portions taken from libpurple/examples/nullclient.c:
 *   Copyright (C) 2007 Sadrul Habib Chowdhury, Sean Egan, Gary Kramlich,
 *                      Mark Doliner, Richard Laager
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

#include "config.h"

#include <string.h>
#include <errno.h>
#include <signal.h>

#include <glib.h>
#include <glib/gstdio.h>

#include <libpurple/account.h>
#include <libpurple/core.h>
#include <libpurple/blist.h>
#include <libpurple/version.h>
#include <libpurple/eventloop.h>
#include <libpurple/prefs.h>
#include <libpurple/util.h>

#ifdef HAVE_PURPLE_DBUS_UNINIT
#  if PURPLE_VERSION_CHECK(2,1,1)
/* Before 2.1.1, this include failed because dbus-types.h was not installed. */
#    include <libpurple/dbus-server.h>
#  else
void purple_dbus_uninit(void);
#  endif
#endif

#include <telepathy-glib/run.h>
#include <telepathy-glib/debug.h>

#include "defines.h"
#include "debug.h"
#include "connection-manager.h"

/* Copied verbatim from nullclient, modulo changing whitespace. */
#define PURPLE_GLIB_READ_COND  (G_IO_IN | G_IO_HUP | G_IO_ERR)
#define PURPLE_GLIB_WRITE_COND (G_IO_OUT | G_IO_HUP | G_IO_ERR | G_IO_NVAL)

typedef struct _PurpleGLibIOClosure {
    PurpleInputFunction function;
    guint result;
    gpointer data;
} PurpleGLibIOClosure;

static void purple_glib_io_destroy(gpointer data)
{
    g_free(data);
}

static gboolean purple_glib_io_invoke(GIOChannel *source,
                                      GIOCondition condition,
                                      gpointer data)
{
    PurpleGLibIOClosure *closure = data;
    PurpleInputCondition purple_cond = 0;

    if (condition & PURPLE_GLIB_READ_COND)
        purple_cond |= PURPLE_INPUT_READ;
    if (condition & PURPLE_GLIB_WRITE_COND)
        purple_cond |= PURPLE_INPUT_WRITE;

    closure->function(closure->data, g_io_channel_unix_get_fd(source),
                      purple_cond);

    return TRUE;
}

static guint glib_input_add(gint fd,
                            PurpleInputCondition condition,
                            PurpleInputFunction function,
                            gpointer data)
{
    PurpleGLibIOClosure *closure = g_new0(PurpleGLibIOClosure, 1);
    GIOChannel *channel;
    GIOCondition cond = 0;

    closure->function = function;
    closure->data = data;

    if (condition & PURPLE_INPUT_READ)
        cond |= PURPLE_GLIB_READ_COND;
    if (condition & PURPLE_INPUT_WRITE)
        cond |= PURPLE_GLIB_WRITE_COND;

    channel = g_io_channel_unix_new(fd);
    closure->result = g_io_add_watch_full(channel, G_PRIORITY_DEFAULT, cond,
            purple_glib_io_invoke, closure, purple_glib_io_destroy);

    g_io_channel_unref(channel);
    return closure->result;
}

static PurpleEventLoopUiOps glib_eventloops = 
{
    g_timeout_add,
    g_source_remove,
    glib_input_add,
    g_source_remove,
    NULL,

    /* padding */
    NULL,
    NULL,
    NULL,
    NULL
};
/*** End of the eventloop functions. ***/

static char *user_dir = NULL;

static void
haze_ui_init ()
{
    purple_accounts_set_ui_ops (haze_get_account_ui_ops ());
    purple_conversations_set_ui_ops (haze_get_conv_ui_ops ());
    purple_connections_set_ui_ops (haze_get_connection_ui_ops ());
}

static PurpleCoreUiOps haze_core_uiops = 
{
    NULL,
    haze_debug_init,
    haze_ui_init,
    NULL,

    /* padding */
    NULL,
    NULL,
    NULL,
    NULL
};

static void
init_libpurple()
{
    user_dir = g_strconcat (g_get_tmp_dir (), G_DIR_SEPARATOR_S,
                                  "haze-XXXXXX", NULL);

    if (!mkdtemp (user_dir)) {
        g_error ("Couldn't make temporary conf directory: %s",
                 strerror (errno));
    }

    purple_util_set_user_dir (user_dir);

    purple_core_set_ui_ops(&haze_core_uiops);

    purple_eventloop_set_ui_ops(&glib_eventloops);

    if (!purple_core_init(UI_ID))
        g_error ("libpurple initialization failed.  :-/");
#ifdef HAVE_PURPLE_DBUS_UNINIT
    /* purple_core_init () calls purple_dbus_init ().  We don't want libpurple's
     * own dbus server, so let's kill it here.  Ideally, it would never be
     * initialized in the first place, but hey.
     */
    purple_dbus_uninit ();
#endif

    purple_set_blist(purple_blist_new());
    purple_blist_load();

    purple_prefs_load();
}

static TpBaseConnectionManager *
get_cm (void)
{
    GLogLevelFlags fatal_mask;

    /* libpurple throws critical errors all over the place because of
     * g_return_val_if_fail().
     * Particularly in MSN.
     * I hate MSN.
     */
    fatal_mask = g_log_set_always_fatal (G_LOG_FATAL_MASK);
    fatal_mask &= ~G_LOG_LEVEL_CRITICAL;
    g_log_set_always_fatal (fatal_mask);

    return (TpBaseConnectionManager *) haze_connection_manager_get ();
}

static gboolean
delete_directory (const char *path)
{
    const gchar *child_path;
    GDir *dir = g_dir_open (path, 0, NULL);
    gboolean ret = TRUE;

    if (!dir)
        return FALSE;

    while (ret && (child_path = g_dir_read_name (dir)))
    {
        gchar *child_full_path =
            g_strconcat (path, G_DIR_SEPARATOR_S, child_path, NULL);
        if (g_file_test (child_full_path, G_FILE_TEST_IS_DIR))
        {
            if (!delete_directory (child_full_path))
                ret = FALSE;
        }
        else
        {
            DEBUG ("deleting %s", child_full_path);
            if (g_unlink (child_full_path))
                ret = FALSE;
        }
        g_free (child_full_path);
    }

    g_dir_close (dir);

    if (ret)
    {
        DEBUG ("deleting %s", path);
        ret = !g_rmdir (path);
    }

    return ret;
}

static void
delete_user_dir (void)
{
    if (!delete_directory (user_dir))
        g_warning ("couldn't delete %s", user_dir);
    g_free (user_dir);
}

int
main(int argc,
     char **argv)
{
    int ret = 0;

    g_set_prgname(UI_ID);

    signal (SIGCHLD, SIG_IGN);
    init_libpurple();

#ifdef HAVE_TP_DEBUG_SET_FLAGS
    tp_debug_set_flags (g_getenv ("HAZE_DEBUG"));
#else
    tp_debug_set_flags_from_env ("HAZE_DEBUG");
#endif

    if (g_getenv ("HAZE_PERSIST"))
    {
#ifdef HAVE_TP_DEBUG_SET_FLAGS
      /* tp-glib >= 0.6.1: persist is no longer a flag in quite the same way */
      tp_debug_set_persistent (TRUE);
#else
      /* tp-glib < 0.6.1: persist is a flag, of sorts */
      tp_debug_set_flags_from_string ("persist");
#endif
    }

    ret = tp_run_connection_manager (UI_ID, PACKAGE_VERSION, get_cm, argc,
                                     argv);

    purple_core_quit ();
    delete_user_dir ();

    return ret;
}

