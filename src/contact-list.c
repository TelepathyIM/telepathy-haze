/*
 * contact-list.c - HazeContactList source
 * Copyright (C) 2007 Will Thompson
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

#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/intset.h>

#include "connection.h"
#include "contact-list.h"
#include "contact-list-channel.h"
#include "debug.h"

typedef struct _HazeContactListPrivate HazeContactListPrivate;
struct _HazeContactListPrivate {
    HazeConnection *conn;

    GHashTable *list_channels;
    GHashTable *group_channels;

    gboolean dispose_has_run;
};

#define HAZE_CONTACT_LIST_GET_PRIVATE(o) \
    ((HazeContactListPrivate *) ((o)->priv))

static void
haze_contact_list_factory_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE(HazeContactList,
    haze_contact_list,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE,
      haze_contact_list_factory_iface_init));

/* properties: */
enum {
    PROP_CONNECTION = 1,

    LAST_PROPERTY
};

static void
haze_contact_list_init (HazeContactList *self)
{
    HazeContactListPrivate *priv =
        (G_TYPE_INSTANCE_GET_PRIVATE((self), HAZE_TYPE_CONTACT_LIST,
                                     HazeContactListPrivate));

    self->priv = priv;

    priv->list_channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                                 NULL, g_object_unref);
    priv->group_channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                                  NULL, g_object_unref);

    priv->dispose_has_run = FALSE;
}

static GObject *
haze_contact_list_constructor (GType type, guint n_props,
                               GObjectConstructParam *props)
{
    GObject *obj;
    /* HazeContactListPrivate *priv; */

    obj = G_OBJECT_CLASS (haze_contact_list_parent_class)->
        constructor (type, n_props, props);
    /* priv = HAZE_CONTACT_LIST_GET_PRIVATE (HAZE_CONTACT_LIST (obj)); */

    return obj;
}

static void
haze_contact_list_dispose (GObject *object)
{
    HazeContactList *self = HAZE_CONTACT_LIST (object);
    HazeContactListPrivate *priv =
        HAZE_CONTACT_LIST_GET_PRIVATE(self);

    if (priv->dispose_has_run)
        return;

    priv->dispose_has_run = TRUE;

    tp_channel_factory_iface_close_all (TP_CHANNEL_FACTORY_IFACE (object));
    g_assert (priv->list_channels == NULL);
    g_assert (priv->group_channels == NULL);

    if (G_OBJECT_CLASS (haze_contact_list_parent_class)->dispose)
        G_OBJECT_CLASS (haze_contact_list_parent_class)->dispose (object);
}

void
haze_contact_list_finalize (GObject *object)
{
/*
    HazeContactList *self = HAZE_CONTACT_LIST (object);
    HazeContactListPrivate *priv =
        HAZE_CONTACT_LIST_GET_PRIVATE(self);
*/

    G_OBJECT_CLASS (haze_contact_list_parent_class)->finalize (object);
}

static void
haze_contact_list_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    HazeContactList *self = HAZE_CONTACT_LIST (object);
    HazeContactListPrivate *priv =
        HAZE_CONTACT_LIST_GET_PRIVATE(self);

    switch (property_id) {
        case PROP_CONNECTION:
            g_value_set_object (value, priv->conn);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
haze_contact_list_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    HazeContactList *self = HAZE_CONTACT_LIST (object);
    HazeContactListPrivate *priv = HAZE_CONTACT_LIST_GET_PRIVATE(self);

    switch (property_id) {
        case PROP_CONNECTION:
            priv->conn = g_value_get_object (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}
static void buddy_added_cb (PurpleBuddy *buddy, gpointer unused);
static void buddy_removed_cb (PurpleBuddy *buddy, gpointer unused);

static void
haze_contact_list_class_init (HazeContactListClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GParamSpec *param_spec;

    object_class->constructor = haze_contact_list_constructor;

    object_class->dispose = haze_contact_list_dispose;
    object_class->finalize = haze_contact_list_finalize;

    object_class->get_property = haze_contact_list_get_property;
    object_class->set_property = haze_contact_list_set_property;

    param_spec = g_param_spec_object ("connection", "HazeConnection object",
                                      "Haze connection object that owns this "
                                      "contact list object.",
                                      HAZE_TYPE_CONNECTION,
                                      G_PARAM_CONSTRUCT_ONLY |
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NICK |
                                      G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

    g_type_class_add_private (object_class,
                              sizeof(HazeContactListPrivate));

    purple_signal_connect (purple_blist_get_handle(), "buddy-added",
                           klass, PURPLE_CALLBACK(buddy_added_cb), NULL);
    purple_signal_connect (purple_blist_get_handle(), "buddy-removed",
                           klass, PURPLE_CALLBACK(buddy_removed_cb), NULL);
}

static void
contact_list_channel_closed_cb (HazeContactListChannel *chan,
                                gpointer user_data)
{
    HazeContactList *contact_list = HAZE_CONTACT_LIST (user_data);
    HazeContactListPrivate *priv = HAZE_CONTACT_LIST_GET_PRIVATE (contact_list);
    TpHandle handle;
    guint handle_type;
    GHashTable *channels;

    g_object_get (chan, "handle", &handle, "handle-type", &handle_type, NULL);
    g_assert (handle_type == TP_HANDLE_TYPE_LIST ||
              handle_type == TP_HANDLE_TYPE_GROUP);

    channels = (handle_type == TP_HANDLE_TYPE_GROUP
        ? priv->group_channels : priv->list_channels);

    if (channels)
    {
        DEBUG ("removing channel %d:%d", handle_type, handle);

        g_hash_table_remove (channels, GINT_TO_POINTER (handle));
    }
}

/**
 * Instantiates the described channel.  Requires that the channel does
 * not already exist.
 */
static HazeContactListChannel *
_haze_contact_list_create_channel (HazeContactList *contact_list,
                                   guint handle_type,
                                   TpHandle handle)
{
    HazeContactListPrivate *priv = HAZE_CONTACT_LIST_GET_PRIVATE(contact_list);
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->conn);
    GHashTable *channels = (handle_type == TP_HANDLE_TYPE_LIST
                           ? priv->list_channels
                           : priv->group_channels);

    HazeContactListChannel *chan;
    const char *name;
    char *mangled_name;
    char *object_path;

    g_assert (handle_type == TP_HANDLE_TYPE_LIST ||
            handle_type == TP_HANDLE_TYPE_GROUP);
    g_assert (channels != NULL);
    g_assert (g_hash_table_lookup (channels, GINT_TO_POINTER (handle)) == NULL);

    name = haze_connection_handle_inspect (priv->conn, handle_type, handle);
    DEBUG ("Instantiating channel %u:%u \"%s\"", handle_type, handle, name);
    mangled_name = tp_escape_as_identifier (name);
    object_path = g_strdup_printf ("%s/ContactListChannel/%s/%s",
                                   base_conn->object_path,
                                   handle_type == TP_HANDLE_TYPE_LIST ? "List"
                                                                      : "Group",
                                   mangled_name);
    g_free (mangled_name);
    mangled_name = NULL;

    chan = g_object_new (HAZE_TYPE_CONTACT_LIST_CHANNEL,
                         "connection", priv->conn,
                         "object-path", object_path,
                         "handle", handle,
                         "handle-type", handle_type,
                         NULL);

    DEBUG ("created %s", object_path);

    g_signal_connect (chan, "closed",
        G_CALLBACK (contact_list_channel_closed_cb), contact_list);

    g_hash_table_insert (channels, GINT_TO_POINTER (handle), chan);

    tp_channel_factory_iface_emit_new_channel (contact_list,
                                               (TpChannelIface *)chan, NULL);
    g_free (object_path);

    return chan;
}

HazeContactListChannel *
haze_contact_list_get_channel (HazeContactList *contact_list,
                               guint handle_type,
                               TpHandle handle,
                               gboolean *created)
{
    HazeContactListPrivate *priv = HAZE_CONTACT_LIST_GET_PRIVATE (contact_list);
    TpBaseConnection *conn = TP_BASE_CONNECTION (priv->conn);
    TpHandleRepoIface *handle_repo =
        tp_base_connection_get_handles (conn, handle_type);
    GHashTable *channels = (handle_type == TP_HANDLE_TYPE_LIST
                           ? priv->list_channels
                           : priv->group_channels);
    HazeContactListChannel *chan;

    g_assert (handle_type == TP_HANDLE_TYPE_LIST ||
              handle_type == TP_HANDLE_TYPE_GROUP);
    g_assert (tp_handle_is_valid (handle_repo, handle, NULL));

    DEBUG ("looking up channel %u:%u '%s'", handle_type, handle,
        tp_handle_inspect (handle_repo, handle));
    chan = g_hash_table_lookup (channels, GINT_TO_POINTER (handle));

    if (chan) {
        if (created)
            *created = FALSE;
    }
    else
    {
        chan = _haze_contact_list_create_channel (contact_list, handle_type,
            handle);
        if (created)
            *created = TRUE;
    }

    return chan;
}

static HazeContactListChannel *
_haze_contact_list_get_group (HazeContactList *contact_list,
                              const char *group_name,
                              gboolean *created)
{
    HazeContactListPrivate *priv = HAZE_CONTACT_LIST_GET_PRIVATE (contact_list);
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->conn);
    TpHandleRepoIface *group_repo = tp_base_connection_get_handles (base_conn,
        TP_HANDLE_TYPE_GROUP);
    TpHandle group_handle = tp_handle_ensure (group_repo, group_name, NULL,
        NULL);
    HazeContactListChannel *group;

    group = haze_contact_list_get_channel (contact_list, TP_HANDLE_TYPE_GROUP,
        group_handle, created);

    tp_handle_unref (group_repo, group_handle);

    return group;
}

static void
haze_contact_list_factory_iface_close_all (TpChannelFactoryIface *iface)
{
    HazeContactList *self = HAZE_CONTACT_LIST (iface);
    HazeContactListPrivate *priv = HAZE_CONTACT_LIST_GET_PRIVATE (self);
    GHashTable *tmp;

    if (priv->list_channels)
    {
        tmp = priv->list_channels;
        priv->list_channels = NULL;
        g_hash_table_destroy (tmp);
    }
    if (priv->group_channels)
    {
        tmp = priv->group_channels;
        priv->group_channels = NULL;
        g_hash_table_destroy (tmp);
    }
}

static void
haze_contact_list_factory_iface_connecting (TpChannelFactoryIface *iface)
{
    /* TODO: Is there anything that should be done here?*/
}

static TpHandleSet *
_handle_a_buddy (HazeConnection *conn,
                 PurpleBuddy *buddy)
{
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
    TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (base_conn,
        TP_HANDLE_TYPE_CONTACT);
    TpHandleSet *set = tp_handle_set_new (contact_repo);

    const gchar *name = purple_buddy_get_name (buddy);
    TpHandle handle = tp_handle_ensure (contact_repo, name,
        NULL, NULL);
    tp_handle_set_add (set, handle);
    tp_handle_unref (contact_repo, handle); /* reffed by set */

    return set;
}

static void
buddy_added_cb (PurpleBuddy *buddy, gpointer unused)
{
    HazeConnection *conn = ACCOUNT_GET_HAZE_CONNECTION (buddy->account);
    HazeContactList *contact_list = conn->contact_list;
    HazeContactListPrivate *priv = HAZE_CONTACT_LIST_GET_PRIVATE (contact_list);
    HazeContactListChannel *subscribe, *group;
    TpHandleSet *add_handles;
    const char *group_name;

    DEBUG ("%s", purple_buddy_get_name (buddy));

    if (TP_BASE_CONNECTION (conn)->status == TP_CONNECTION_STATUS_DISCONNECTED)
    {
        DEBUG ("disconnected, ignoring");
        return;
    }

    add_handles = _handle_a_buddy (priv->conn, buddy);

    subscribe = haze_contact_list_get_channel (contact_list,
        TP_HANDLE_TYPE_LIST, HAZE_LIST_HANDLE_SUBSCRIBE, NULL);

    tp_group_mixin_change_members (G_OBJECT (subscribe), "",
        tp_handle_set_peek (add_handles), NULL, NULL, NULL, 0, 0);

    group_name = purple_group_get_name (purple_buddy_get_group (buddy));
    group = _haze_contact_list_get_group (contact_list, group_name, NULL);

    tp_group_mixin_change_members (G_OBJECT (group), "",
        tp_handle_set_peek (add_handles), NULL, NULL, NULL, 0, 0);

    tp_handle_set_destroy (add_handles);
}

static void
buddy_removed_cb (PurpleBuddy *buddy, gpointer unused)
{
    HazeConnection *conn = ACCOUNT_GET_HAZE_CONNECTION (buddy->account);
    HazeContactList *contact_list = conn->contact_list;
    HazeContactListPrivate *priv = HAZE_CONTACT_LIST_GET_PRIVATE (contact_list);
    HazeContactListChannel *subscribe, *group;
    TpHandleSet *rem_handles;
    const char *group_name, *buddy_name;
    GSList *buddies, *l;
    gboolean last_instance = TRUE;

    /* Every buddy gets removed after disconnection, because the PurpleAccount
     * gets deleted.  So let's ignore removals when we're offline.
     */
    if (TP_BASE_CONNECTION (conn)->status == TP_CONNECTION_STATUS_DISCONNECTED)
        return;

    buddy_name = purple_buddy_get_name (buddy);
    DEBUG ("%s", buddy_name);

    rem_handles = _handle_a_buddy (priv->conn, buddy);

    buddies = purple_find_buddies (priv->conn->account, buddy_name);
    for (l = buddies; l != NULL; l = l->next)
    {
        if (l->data != buddy)
        {
            last_instance = FALSE;
            break;
        }
    }
    g_slist_free (buddies);

    if (last_instance)
    {
        subscribe = haze_contact_list_get_channel (contact_list,
            TP_HANDLE_TYPE_LIST, HAZE_LIST_HANDLE_SUBSCRIBE, NULL);

        tp_group_mixin_change_members (G_OBJECT (subscribe), "",
            NULL, tp_handle_set_peek (rem_handles), NULL, NULL, 0, 0);
    }

    group_name = purple_group_get_name (purple_buddy_get_group (buddy));
    group = _haze_contact_list_get_group (contact_list, group_name, NULL);

    tp_group_mixin_change_members (G_OBJECT (group), "",
        NULL, tp_handle_set_peek (rem_handles), NULL, NULL, 0, 0);

    tp_handle_set_destroy (rem_handles);
}

typedef struct _HandleContext {
    TpHandleRepoIface *contact_repo;
    TpHandleSet *add_handles;
    GHashTable *group_handles;
} HandleContext;

static void
_initial_buddies_foreach (PurpleBuddy *buddy,
                          HandleContext *context)
{
    const gchar *name = purple_buddy_get_name (buddy);
    PurpleGroup *group = purple_buddy_get_group (buddy);
    gchar *group_name = group->name;
    TpIntSet *group_set;
    TpHandle handle;

    handle = tp_handle_ensure (context->contact_repo, name, NULL, NULL);
    tp_handle_set_add (context->add_handles, handle);

    group_set = g_hash_table_lookup (context->group_handles, group_name);

    if (!group_set) {
        group_set = tp_intset_new ();
        g_hash_table_insert (context->group_handles, group_name, group_set);
    }

    tp_intset_add (group_set, handle);

    tp_handle_unref (context->contact_repo, handle);

}

gboolean
_create_initial_group(gchar *group_name,
                      TpIntSet *handles,
                      HazeContactList *contact_list)
{
    HazeContactListPrivate *priv = HAZE_CONTACT_LIST_GET_PRIVATE(contact_list);
    TpBaseConnection *conn = TP_BASE_CONNECTION (priv->conn);
    TpHandleRepoIface *handle_repo =
        tp_base_connection_get_handles (conn, TP_HANDLE_TYPE_GROUP);
    HazeContactListChannel *group;
    TpHandle group_handle = tp_handle_ensure (handle_repo, group_name, NULL,
        NULL);
    group = haze_contact_list_get_channel (contact_list,
        TP_HANDLE_TYPE_GROUP, group_handle, NULL /*created*/);

    tp_group_mixin_change_members (G_OBJECT (group), "", handles, NULL,
                                   NULL, NULL, 0, 0);

    tp_handle_unref (handle_repo, group_handle); /* reffed by group */

    return TRUE;
}

static void
_add_initial_buddies (HazeContactList *self)
{
    HazeContactListPrivate *priv = HAZE_CONTACT_LIST_GET_PRIVATE (self);
    HazeContactListChannel *subscribe;

    PurpleAccount *account = priv->conn->account;

    GSList *buddies = purple_find_buddies(account, NULL);
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->conn);
    TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (base_conn,
        TP_HANDLE_TYPE_CONTACT);
    TpHandleSet *add_handles = tp_handle_set_new (contact_repo);
    /* Map from group names (const char *) to (TpIntSet *)s of handles */
    GHashTable *group_handles = g_hash_table_new_full (NULL, NULL, NULL,
        (GDestroyNotify) tp_intset_destroy);
    HandleContext context = { contact_repo, add_handles, group_handles };

    g_slist_foreach (buddies, (GFunc) _initial_buddies_foreach, &context);
    g_slist_free (buddies);

    subscribe = haze_contact_list_get_channel (self, TP_HANDLE_TYPE_LIST,
            HAZE_LIST_HANDLE_SUBSCRIBE, NULL /*created*/);

    tp_group_mixin_change_members (G_OBJECT (subscribe), "",
        tp_handle_set_peek (add_handles), NULL, NULL, NULL, 0, 0);

    g_hash_table_foreach_remove (group_handles, (GHRFunc) _create_initial_group,
        self);

    tp_handle_set_destroy (add_handles);
}

static void
haze_contact_list_factory_iface_connected (TpChannelFactoryIface *iface)
{
    HazeContactList *self = HAZE_CONTACT_LIST (iface);

    _add_initial_buddies (self);

}

static void
haze_contact_list_factory_iface_disconnected (TpChannelFactoryIface *iface)
{
}

struct _ForeachData
{
    TpChannelFunc foreach;
    gpointer user_data;
};

static void
_foreach_slave (gpointer key, gpointer value, gpointer user_data)
{
    struct _ForeachData *data = (struct _ForeachData *) user_data;
    TpChannelIface *chan = TP_CHANNEL_IFACE (value);

    data->foreach (chan, data->user_data);
}

static void
haze_contact_list_factory_iface_foreach (TpChannelFactoryIface *iface,
                                         TpChannelFunc foreach,
                                         gpointer user_data)
{
    HazeContactList *self = HAZE_CONTACT_LIST (iface);
    HazeContactListPrivate *priv = HAZE_CONTACT_LIST_GET_PRIVATE (self);
    struct _ForeachData data;

    data.user_data = user_data;
    data.foreach = foreach;

    g_hash_table_foreach (priv->list_channels, _foreach_slave, &data);
    g_hash_table_foreach (priv->group_channels, _foreach_slave, &data);
}

static TpChannelFactoryRequestStatus
haze_contact_list_factory_iface_request (TpChannelFactoryIface *iface,
                                         const gchar *chan_type,
                                         TpHandleType handle_type,
                                         guint handle,
                                         gpointer request,
                                         TpChannelIface **ret,
                                         GError **error)
{
    HazeContactList *self = HAZE_CONTACT_LIST (iface);
    HazeContactListPrivate *priv = HAZE_CONTACT_LIST_GET_PRIVATE (self);
    TpBaseConnection *conn = TP_BASE_CONNECTION (priv->conn);
    TpHandleRepoIface *handle_repo =
        tp_base_connection_get_handles (conn, handle_type);
    HazeContactListChannel *chan;
    const gchar *channel_name;
    gboolean created;

    if (strcmp (chan_type, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST))
        return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;

    if (handle_type != TP_HANDLE_TYPE_LIST &&
        handle_type != TP_HANDLE_TYPE_GROUP)
        return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;

    /* If the handle is valid, then they are not requesting an unimplemented
     * list channel.
     */
    if (!tp_handle_is_valid (handle_repo, handle, NULL))
        return TP_CHANNEL_FACTORY_REQUEST_STATUS_INVALID_HANDLE;

    channel_name = tp_handle_inspect (handle_repo, handle);
    DEBUG ("grabbing channel '%s'...", channel_name);
    chan = haze_contact_list_get_channel (self, handle_type, handle,
        &created);

    if (chan)
    {
        *ret = TP_CHANNEL_IFACE (chan);
        if (created)
        {
            return TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED;
        }
        else
        {
            return TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING;
        }
    }
    else
    {
        g_warning ("eh!  why does the channel '%s' not exist?", channel_name);
        return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;
    }
}

static void
haze_contact_list_factory_iface_init (gpointer g_iface,
                                      gpointer iface_data)
{
    TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *) g_iface;

    klass->close_all = haze_contact_list_factory_iface_close_all;
    klass->connecting = haze_contact_list_factory_iface_connecting;
    klass->connected = haze_contact_list_factory_iface_connected;
    klass->disconnected = haze_contact_list_factory_iface_disconnected;
    klass->foreach = haze_contact_list_factory_iface_foreach;
    klass->request = haze_contact_list_factory_iface_request;
}
