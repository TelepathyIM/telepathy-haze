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

#include <telepathy-glib/channel-manager.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/intset.h>

#include "connection.h"
#include "contact-list.h"
#include "contact-list-channel.h"
#include "debug.h"

typedef struct _PublishRequestData PublishRequestData;

/** PublishRequestData:
 *
 *  Keeps track of the relevant callbacks to approve or deny a contact's publish
 *  request.
 */
struct _PublishRequestData {
    HazeContactList *self;
    HazeContactListChannel *publish;
    TpHandle handle;
    gchar *message;

    PurpleAccountRequestAuthorizationCb allow;
    PurpleAccountRequestAuthorizationCb deny;
    gpointer data;
};


static PublishRequestData *
publish_request_data_new (void)
{
    return g_slice_new0 (PublishRequestData);
}

static void
publish_request_data_free (PublishRequestData *prd)
{
    g_object_unref (prd->publish);
    g_free (prd->message);
    g_slice_free (PublishRequestData, prd);
}

struct _HazeContactListPrivate {
    HazeConnection *conn;

    GHashTable *list_channels;
    GHashTable *group_channels;
    gulong status_changed_id;

    /* Maps TpHandle to PublishRequestData, corresponding to the handles on
     * publish's local_pending.
     */
    GHashTable *pending_publish_requests;

    /* Contacts whose publish requests we've accepted or declined. */
    TpHandleSet *publishing_to;
    TpHandleSet *not_publishing_to;

    gboolean dispose_has_run;
};

static void channel_manager_iface_init (gpointer, gpointer);

G_DEFINE_TYPE_WITH_CODE(HazeContactList,
    haze_contact_list,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_MANAGER,
      channel_manager_iface_init))

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

static void haze_contact_list_close_all (HazeContactList *self);

static void _add_initial_buddies (HazeContactList *self,
    HazeContactListChannel *subscribe, HazeContactListChannel *stored);

static void
status_changed_cb (HazeConnection *conn,
                   guint status,
                   guint reason,
                   HazeContactList *self)
{
    switch (status)
    {
    case TP_CONNECTION_STATUS_DISCONNECTED:
        haze_contact_list_close_all (self);
        break;
    case TP_CONNECTION_STATUS_CONNECTED:
        {
            HazeContactListChannel *subscribe, *publish, *stored;

            /* Ensure contact lists exist before going any further. */
            subscribe = haze_contact_list_get_channel (self,
                TP_HANDLE_TYPE_LIST, HAZE_LIST_HANDLE_SUBSCRIBE,
                NULL, NULL /*created*/);
            g_assert (subscribe != NULL);

            /* Ensure contact lists exist before going any further. */
            stored = haze_contact_list_get_channel (self,
                TP_HANDLE_TYPE_LIST, HAZE_LIST_HANDLE_STORED,
                NULL, NULL /*created*/);
            g_assert (stored != NULL);

            publish = haze_contact_list_get_channel (self, TP_HANDLE_TYPE_LIST,
                    HAZE_LIST_HANDLE_PUBLISH, NULL, NULL /*created*/);
            g_assert (publish != NULL);

            _add_initial_buddies (self, subscribe, stored);
        }
        break;
    }
}

static GObject *
haze_contact_list_constructor (GType type, guint n_props,
                               GObjectConstructParam *props)
{
    GObject *obj;
    HazeContactList *self;
    TpHandleRepoIface *contact_repo;

    obj = G_OBJECT_CLASS (haze_contact_list_parent_class)->
        constructor (type, n_props, props);

    self = HAZE_CONTACT_LIST (obj);
    self->priv->status_changed_id = g_signal_connect (self->priv->conn,
        "status-changed", (GCallback) status_changed_cb, self);

    contact_repo = tp_base_connection_get_handles (
        (TpBaseConnection *) self->priv->conn, TP_HANDLE_TYPE_CONTACT);

    self->priv->publishing_to = tp_handle_set_new (contact_repo);
    self->priv->not_publishing_to = tp_handle_set_new (contact_repo);

    self->priv->pending_publish_requests = g_hash_table_new_full (NULL, NULL,
        NULL, (GDestroyNotify) publish_request_data_free);

    return obj;
}

static void
haze_contact_list_dispose (GObject *object)
{
    HazeContactList *self = HAZE_CONTACT_LIST (object);
    HazeContactListPrivate *priv = self->priv;

    if (priv->dispose_has_run)
        return;

    priv->dispose_has_run = TRUE;

    haze_contact_list_close_all (self);
    g_assert (priv->list_channels == NULL);
    g_assert (priv->group_channels == NULL);

    tp_clear_pointer (&priv->publishing_to, tp_handle_set_destroy);
    tp_clear_pointer (&priv->not_publishing_to, tp_handle_set_destroy);

    if (priv->pending_publish_requests)
    {
        g_assert (g_hash_table_size (priv->pending_publish_requests) == 0);
        g_hash_table_destroy (priv->pending_publish_requests);
        priv->pending_publish_requests = NULL;
    }

    if (G_OBJECT_CLASS (haze_contact_list_parent_class)->dispose)
        G_OBJECT_CLASS (haze_contact_list_parent_class)->dispose (object);
}

static void
haze_contact_list_finalize (GObject *object)
{
    G_OBJECT_CLASS (haze_contact_list_parent_class)->finalize (object);
}

static void
haze_contact_list_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    HazeContactList *self = HAZE_CONTACT_LIST (object);
    HazeContactListPrivate *priv = self->priv;

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
    HazeContactListPrivate *priv = self->priv;

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
    HazeContactListPrivate *priv = contact_list->priv;
    TpHandle handle;
    guint handle_type;
    GHashTable *channels;

    tp_channel_manager_emit_channel_closed_for_object (contact_list,
        TP_EXPORTABLE_CHANNEL (chan));

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
                                   TpHandle handle,
                                   gpointer request_token)
{
    HazeContactListPrivate *priv = contact_list->priv;
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->conn);
    GHashTable *channels = (handle_type == TP_HANDLE_TYPE_LIST
                           ? priv->list_channels
                           : priv->group_channels);

    HazeContactListChannel *chan;
    const char *name;
    char *mangled_name;
    char *object_path;
    GSList *requests = NULL;

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

    if (handle_type == TP_HANDLE_TYPE_GROUP)
      purple_group_new (name);

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

    if (request_token != NULL)
        requests = g_slist_prepend (requests, request_token);

    tp_channel_manager_emit_new_channel (contact_list,
        TP_EXPORTABLE_CHANNEL (chan), requests);
    g_slist_free (requests);

    g_free (object_path);

    return chan;
}

struct foreach_data {
    TpExportableChannelFunc func;
    gpointer data;
};

static void
foreach_helper (gpointer k,
                gpointer v,
                gpointer data)
{
  struct foreach_data *foreach = data;

  foreach->func (TP_EXPORTABLE_CHANNEL (v), foreach->data);
}

static void
haze_contact_list_foreach_channel (TpChannelManager *manager,
                                   TpExportableChannelFunc func,
                                   gpointer data)
{
    HazeContactList *self = HAZE_CONTACT_LIST (manager);
    struct foreach_data foreach = { func, data };

    g_hash_table_foreach (self->priv->group_channels, foreach_helper,
        &foreach);
    g_hash_table_foreach (self->priv->list_channels, foreach_helper,
        &foreach);
}

static const gchar * const list_fixed_properties[] = {
    TP_IFACE_CHANNEL ".ChannelType",
    TP_IFACE_CHANNEL ".TargetHandleType",
    NULL
};
static const gchar * const list_allowed_properties[] = {
    TP_IFACE_CHANNEL ".TargetHandle",
    TP_IFACE_CHANNEL ".TargetID",
    NULL
};
static const gchar * const *group_fixed_properties = list_fixed_properties;
static const gchar * const *group_allowed_properties = list_allowed_properties;

static void
haze_contact_list_foreach_channel_class (TpChannelManager *manager,
                                         TpChannelManagerChannelClassFunc func,
                                         gpointer user_data)
{
    GHashTable *table = g_hash_table_new_full (g_str_hash, g_str_equal,
        NULL, (GDestroyNotify) tp_g_value_slice_free);
    GValue *value, *handle_type_value;

    value = tp_g_value_slice_new (G_TYPE_STRING);
    g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST);
    g_hash_table_insert (table, TP_IFACE_CHANNEL ".ChannelType", value);

    handle_type_value = tp_g_value_slice_new (G_TYPE_UINT);
    g_hash_table_insert (table, TP_IFACE_CHANNEL ".TargetHandleType",
        handle_type_value);

    g_value_set_uint (handle_type_value, TP_HANDLE_TYPE_LIST);
    func (manager, table, list_allowed_properties, user_data);

    g_value_set_uint (handle_type_value, TP_HANDLE_TYPE_GROUP);
    func (manager, table, group_allowed_properties, user_data);

    g_hash_table_destroy (table);
}

HazeContactListChannel *
haze_contact_list_get_channel (HazeContactList *contact_list,
                               guint handle_type,
                               TpHandle handle,
                               gpointer request_token,
                               gboolean *created)
{
    HazeContactListPrivate *priv = contact_list->priv;
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
            handle, request_token);
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
    HazeContactListPrivate *priv = contact_list->priv;
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->conn);
    TpHandleRepoIface *group_repo = tp_base_connection_get_handles (base_conn,
        TP_HANDLE_TYPE_GROUP);
    TpHandle group_handle = tp_handle_ensure (group_repo, group_name, NULL,
        NULL);
    HazeContactListChannel *group;

    group = haze_contact_list_get_channel (contact_list, TP_HANDLE_TYPE_GROUP,
        group_handle, NULL, created);

    tp_handle_unref (group_repo, group_handle);

    return group;
}

static void
haze_contact_list_close_all (HazeContactList *self)
{
    HazeContactListPrivate *priv = self->priv;
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

    if (priv->status_changed_id != 0)
    {
        g_signal_handler_disconnect (priv->conn, priv->status_changed_id);
        priv->status_changed_id = 0;
    }
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
    HazeContactListPrivate *priv = contact_list->priv;
    HazeContactListChannel *subscribe, *stored, *group;
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
        TP_HANDLE_TYPE_LIST, HAZE_LIST_HANDLE_SUBSCRIBE, NULL, NULL);
    stored = haze_contact_list_get_channel (contact_list,
        TP_HANDLE_TYPE_LIST, HAZE_LIST_HANDLE_STORED, NULL, NULL);

    tp_group_mixin_change_members (G_OBJECT (subscribe), "",
        tp_handle_set_peek (add_handles), NULL, NULL, NULL, 0, 0);
    tp_group_mixin_change_members (G_OBJECT (stored), "",
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
    HazeContactList *contact_list;
    HazeContactListPrivate *priv;
    HazeContactListChannel *subscribe, *stored, *group;
    TpHandleSet *rem_handles;
    const char *group_name, *buddy_name;
    GSList *buddies, *l;
    gboolean last_instance = TRUE;

    /* Every buddy gets removed after disconnection, because the PurpleAccount
     * gets deleted.  So let's ignore removals when we're offline.
     */
    if (TP_BASE_CONNECTION (conn)->status == TP_CONNECTION_STATUS_DISCONNECTED)
        return;

    contact_list = conn->contact_list;
    priv = contact_list->priv;

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
            TP_HANDLE_TYPE_LIST, HAZE_LIST_HANDLE_SUBSCRIBE, NULL, NULL);
        stored = haze_contact_list_get_channel (contact_list,
            TP_HANDLE_TYPE_LIST, HAZE_LIST_HANDLE_STORED, NULL, NULL);

        tp_group_mixin_change_members (G_OBJECT (subscribe), "",
            NULL, tp_handle_set_peek (rem_handles), NULL, NULL, 0, 0);
        tp_group_mixin_change_members (G_OBJECT (stored), "",
            NULL, tp_handle_set_peek (rem_handles), NULL, NULL, 0, 0);
    }

    group_name = purple_group_get_name (purple_buddy_get_group (buddy));
    group = _haze_contact_list_get_group (contact_list, group_name, NULL);

    tp_group_mixin_change_members (G_OBJECT (group), "",
        NULL, tp_handle_set_peek (rem_handles), NULL, NULL, 0, 0);

    tp_handle_set_destroy (rem_handles);
}


/* Objects needed or populated while iterating across the purple buddy list at
 * login.
 */
typedef struct _HandleContext {
    TpHandleRepoIface *contact_repo;
    TpHandleSet *add_handles;

    /* Map from group names (const char *) to (TpIntSet *)s of handles */
    GHashTable *group_handles;
} HandleContext;


/* Called for each buddy on the purple buddy list at login.  Adds them to the
 * handle set that will become the 'subscribe' list, and to the handle set
 * which will become their group's channel.
 */
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


/* Creates a group channel on contact_list called group_name, containing the
 * supplied handles.  Called while traversing the buddy list at login.
 */
static gboolean
_create_initial_group(gchar *group_name,
                      TpIntSet *handles,
                      HazeContactList *contact_list)
{
    HazeContactListPrivate *priv = contact_list->priv;
    TpBaseConnection *conn = TP_BASE_CONNECTION (priv->conn);
    TpHandleRepoIface *handle_repo =
        tp_base_connection_get_handles (conn, TP_HANDLE_TYPE_GROUP);
    HazeContactListChannel *group;
    TpHandle group_handle = tp_handle_ensure (handle_repo, group_name, NULL,
        NULL);
    group = haze_contact_list_get_channel (contact_list,
        TP_HANDLE_TYPE_GROUP, group_handle, NULL, NULL /*created*/);

    tp_group_mixin_change_members (G_OBJECT (group), "", handles, NULL,
                                   NULL, NULL, 0, 0);

    tp_handle_unref (handle_repo, group_handle); /* reffed by group */

    /* Remove this group from the hash of groups to be constructed. */
    return TRUE;
}


/* Iterates across all buddies on a purple account's buddy list at login,
 * adding them to subscribe.
 */
static void
_add_initial_buddies (HazeContactList *self,
                      HazeContactListChannel *subscribe,
                      HazeContactListChannel *stored)
{
    HazeContactListPrivate *priv = self->priv;

    PurpleAccount *account = priv->conn->account;
    GSList *buddies = purple_find_buddies(account, NULL);

    TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->conn);
    TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (base_conn,
        TP_HANDLE_TYPE_CONTACT);

    TpHandleSet *add_handles = tp_handle_set_new (contact_repo);
    GHashTable *group_handles = g_hash_table_new_full (NULL, NULL, NULL,
        (GDestroyNotify) tp_intset_destroy);
    HandleContext context = { contact_repo, add_handles, group_handles };

    g_slist_foreach (buddies, (GFunc) _initial_buddies_foreach, &context);
    g_slist_free (buddies);

    tp_group_mixin_change_members (G_OBJECT (subscribe), "",
        tp_handle_set_peek (add_handles), NULL, NULL, NULL, 0, 0);
    tp_group_mixin_change_members (G_OBJECT (stored), "",
        tp_handle_set_peek (add_handles), NULL, NULL, NULL, 0, 0);

    g_hash_table_foreach_remove (group_handles, (GHRFunc) _create_initial_group,
        self);
    g_hash_table_destroy (group_handles);

    tp_handle_set_destroy (add_handles);
}

static gboolean
haze_contact_list_request (HazeContactList *self,
                           gpointer request_token,
                           GHashTable *request_properties,
                           gboolean require_new)
{
    TpHandleRepoIface *handle_repo;
    TpHandleType handle_type;
    TpHandle handle;
    const gchar *channel_name;
    gboolean created;
    HazeContactListChannel *chan;
    GError *error = NULL;
    const gchar * const *fixed_properties;
    const gchar * const *allowed_properties;

    if (tp_strdiff (tp_asv_get_string (request_properties,
            TP_IFACE_CHANNEL ".ChannelType"),
        TP_IFACE_CHANNEL_TYPE_CONTACT_LIST))
    {
        return FALSE;
    }

    handle_type = tp_asv_get_uint32 (request_properties,
        TP_IFACE_CHANNEL ".TargetHandleType", NULL);

    switch (handle_type)
    {
    case TP_HANDLE_TYPE_LIST:
        fixed_properties = list_fixed_properties;
        allowed_properties = list_allowed_properties;
        break;
    case TP_HANDLE_TYPE_GROUP:
        fixed_properties = group_fixed_properties;
        allowed_properties = group_allowed_properties;
        break;
    default:
        return FALSE;
    }

    handle_repo = tp_base_connection_get_handles (
        TP_BASE_CONNECTION (self->priv->conn), handle_type);

    handle = tp_asv_get_uint32 (request_properties,
        TP_IFACE_CHANNEL ".TargetHandle", NULL);
    g_assert (tp_handle_is_valid (handle_repo, handle, NULL));

    if (tp_channel_manager_asv_has_unknown_properties (request_properties,
          fixed_properties, allowed_properties, &error))
    {
        goto error;
    }

    channel_name = tp_handle_inspect (handle_repo, handle);
    DEBUG ("grabbing channel '%s'...", channel_name);

    chan = haze_contact_list_get_channel (self, handle_type, handle,
        request_token, &created);
    g_assert (chan != NULL);

    if (!created)
    {
        if (require_new)
        {
            tp_channel_manager_emit_request_failed (self, request_token,
                TP_ERRORS, TP_ERROR_NOT_AVAILABLE, "Channel already exists");
        }
        else
        {
            tp_channel_manager_emit_request_already_satisfied (self,
                request_token, TP_EXPORTABLE_CHANNEL (chan));
        }
    }

    return TRUE;

error:
    tp_channel_manager_emit_request_failed (self, request_token,
        error->domain, error->code, error->message);
    g_error_free (error);
    return TRUE;
}


static gboolean
haze_contact_list_create_channel (TpChannelManager *manager,
                                  gpointer request_token,
                                  GHashTable *request_properties)
{
    HazeContactList *self = HAZE_CONTACT_LIST (manager);

    return haze_contact_list_request (self, request_token,
        request_properties, TRUE);
}

static gboolean
haze_contact_list_ensure_channel (TpChannelManager *manager,
                                  gpointer request_token,
                                  GHashTable *request_properties)
{
    HazeContactList *self = HAZE_CONTACT_LIST (manager);

    return haze_contact_list_request (self, request_token,
        request_properties, FALSE);
}

static void
channel_manager_iface_init (gpointer g_iface,
                            gpointer iface_data G_GNUC_UNUSED)
{
    TpChannelManagerIface *iface = g_iface;

    iface->foreach_channel = haze_contact_list_foreach_channel;
    iface->foreach_channel_class = haze_contact_list_foreach_channel_class;
    iface->create_channel = haze_contact_list_create_channel;
    iface->ensure_channel = haze_contact_list_ensure_channel;
    /* Request is equivalent to Ensure for this channel class */
    iface->request_channel = haze_contact_list_ensure_channel;
}


/* Removes the PublishRequestData for the given handle, from the
 * pending_publish_requests table, dropping its reference to that handle.
 */
static void
remove_pending_publish_request (HazeContactList *self,
                                TpHandle handle)
{
    HazeConnection *conn = self->priv->conn;
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
    TpHandleRepoIface *handle_repo =
        tp_base_connection_get_handles (base_conn, TP_HANDLE_TYPE_CONTACT);

    gpointer h = GUINT_TO_POINTER (handle);
    gboolean removed;

    removed = g_hash_table_remove (self->priv->pending_publish_requests, h);
    g_assert (removed);

    tp_handle_unref (handle_repo, handle);
}

void
haze_contact_list_accept_publish_request (HazeContactList *self,
    TpHandle handle)
{
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (self->priv->conn);
  gpointer key = GUINT_TO_POINTER (handle);
  PublishRequestData *request_data = g_hash_table_lookup (
      self->priv->pending_publish_requests, key);
  TpIntSet *add;
  const gchar *bname = haze_connection_handle_inspect (self->priv->conn,
      TP_HANDLE_TYPE_CONTACT, handle);

  if (request_data == NULL)
    return;

  DEBUG ("allowing publish request for %s", bname);
  request_data->allow(request_data->data);

  add = tp_intset_new_containing (handle);
  tp_group_mixin_change_members (G_OBJECT (request_data->publish), "",
      add, NULL, NULL, NULL, base_conn->self_handle,
      TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
  tp_intset_destroy (add);

  tp_handle_set_add (self->priv->publishing_to, handle);

  remove_pending_publish_request (self, handle);
}

void
haze_contact_list_reject_publish_request (HazeContactList *self,
    TpHandle handle)
{
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (self->priv->conn);
  gpointer key = GUINT_TO_POINTER (handle);
  PublishRequestData *request_data = g_hash_table_lookup (
      self->priv->pending_publish_requests, key);
  TpIntSet *to_remove;
  const gchar *bname = haze_connection_handle_inspect (self->priv->conn,
      TP_HANDLE_TYPE_CONTACT, handle);

  if (request_data == NULL)
    return;

  DEBUG ("denying publish request for %s", bname);
  request_data->deny(request_data->data);

  to_remove = tp_intset_new_containing (handle);
  tp_group_mixin_change_members (G_OBJECT (request_data->publish), "",
      NULL, to_remove, NULL, NULL, base_conn->self_handle,
      TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
  tp_intset_destroy (to_remove);

  tp_handle_set_add (self->priv->not_publishing_to, handle);

  remove_pending_publish_request (self, handle);
}


gpointer
haze_request_authorize (PurpleAccount *account,
                        const char *remote_user,
                        const char *id,
                        const char *alias,
                        const char *message,
                        gboolean on_list,
                        PurpleAccountRequestAuthorizationCb authorize_cb,
                        PurpleAccountRequestAuthorizationCb deny_cb,
                        void *user_data)
{
    HazeConnection *conn = account->ui_data;
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
    TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (base_conn,
        TP_HANDLE_TYPE_CONTACT);
    HazeContactList *self = conn->contact_list;
    HazeContactListChannel *publish =
        haze_contact_list_get_channel (conn->contact_list,
            TP_HANDLE_TYPE_LIST, HAZE_LIST_HANDLE_PUBLISH, NULL, NULL);

    TpIntSet *add_local_pending = tp_intset_new ();
    TpHandle remote_handle;
    gboolean changed;
    PublishRequestData *request_data = publish_request_data_new ();

    /* This handle is owned by request_data, and is unreffed in
     * remove_pending_publish_request.
     */
    remote_handle = tp_handle_ensure (contact_repo, remote_user, NULL, NULL);
    request_data->self = g_object_ref (conn->contact_list);
    request_data->publish = g_object_ref (publish);
    request_data->handle = remote_handle;
    request_data->allow = authorize_cb;
    request_data->deny = deny_cb;
    request_data->data = user_data;
    request_data->message = g_strdup (message);

    g_hash_table_insert (conn->contact_list->priv->pending_publish_requests,
        GUINT_TO_POINTER (remote_handle), request_data);

    /* If we got a publish request from them, then presumably we weren't
     * already publishing to them? */
    tp_handle_set_remove (self->priv->publishing_to, remote_handle);
    tp_handle_set_add (self->priv->not_publishing_to, remote_handle);

    tp_intset_add (add_local_pending, remote_handle);
    changed = tp_group_mixin_change_members (G_OBJECT (publish), message, NULL,
        NULL, add_local_pending, NULL, remote_handle,
        TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
    tp_intset_destroy (add_local_pending);

    return request_data;
}


void
haze_close_account_request (gpointer request_data_)
{
    PublishRequestData *request_data = request_data_;

    /* When 'request_data' is removed from the pending request table, its
     * reference to 'publish' is dropped.  So, we take our own reference here,
     * in case the reference in 'request_data' was the last one.  (If we don't,
     * the channel (including 'pending_publish_requests') is destroyed half-way
     * through g_hash_table_remove() in remove_pending_publish_request(); cue
     * stack corruption.)
     */
    HazeContactList *self = g_object_ref (request_data->self);
    HazeContactListChannel *publish = g_object_ref (request_data->publish);

    TpBaseConnection *base_conn = TP_BASE_CONNECTION (self->priv->conn);

    TpIntSet *to_remove = tp_intset_new ();

    DEBUG ("cancelling publish request for handle %u", request_data->handle);

    tp_intset_add (to_remove, request_data->handle);
    tp_group_mixin_change_members (G_OBJECT (publish), NULL, NULL, to_remove,
        NULL, NULL, base_conn->self_handle,
        TP_CHANNEL_GROUP_CHANGE_REASON_NONE);
    tp_intset_destroy (to_remove);

    remove_pending_publish_request (self, request_data->handle);

    g_object_unref (publish);
    g_object_unref (self);
}

void
haze_contact_list_request_subscription (HazeContactList *self,
    TpHandle handle,
    const gchar *message)
{
  PurpleAccount *account = self->priv->conn->account;
  const gchar *bname = haze_connection_handle_inspect (self->priv->conn,
      TP_HANDLE_TYPE_CONTACT, handle);
  PurpleBuddy *buddy;

  /* If the buddy already exists, then it should already be on the
   * subscribe list.
   */
  g_assert (purple_find_buddy (account, bname) == NULL);

  buddy = purple_buddy_new (account, bname, NULL);

  /* FIXME: This emits buddy-added at once, so a buddy will never be
   * on the pending list.  It doesn't look like libpurple even has
   * the concept of a pending buddy.  Sigh.
   */
  purple_blist_add_buddy (buddy, NULL, NULL, NULL);
  purple_account_add_buddy (account, buddy);
}

void
haze_contact_list_remove_contact (HazeContactList *self,
    TpHandle handle)
{
  PurpleAccount *account = self->priv->conn->account;
  const gchar *bname = haze_connection_handle_inspect (self->priv->conn,
      TP_HANDLE_TYPE_CONTACT, handle);
  GSList *buddies, *l;

  buddies = purple_find_buddies (account, bname);
  /* buddies may be NULL, but that's a perfectly reasonable GSList */

  /* Removing a buddy from subscribe entails removing it from all
   * groups since you can't have a buddy without groups in libpurple.
   */
  for (l = buddies; l != NULL; l = l->next)
    {
      PurpleBuddy *buddy = (PurpleBuddy *) l->data;
      PurpleGroup *group = purple_buddy_get_group (buddy);

      purple_account_remove_buddy (account, buddy, group);
      purple_blist_remove_buddy (buddy);
    }

  /* Also decline any publication requests we might have had */
  haze_contact_list_reject_publish_request (self, handle);

  g_slist_free (buddies);
}

void
haze_contact_list_add_to_group (HazeContactList *self,
    const gchar *group_name,
    TpHandle handle)
{
    HazeConnection *conn = self->priv->conn;
    const gchar *bname =
        haze_connection_handle_inspect (conn, TP_HANDLE_TYPE_CONTACT, handle);
    PurpleBuddy *buddy;
    /* This is correct, despite the naming: it returns a borrowed reference
     * to an existing group (if possible) or to a new group (otherwise). */
    PurpleGroup *group = purple_group_new (group_name);

    g_return_if_fail (group != NULL);

    /* If the buddy is already in this group then this callback should
     * never have been called.
     */
    g_assert (purple_find_buddy_in_group (conn->account, bname, group)
        == NULL);

    buddy = purple_buddy_new (conn->account, bname, NULL);

    /* FIXME: This causes it to be added to 'subscribed' too. */
    purple_blist_add_buddy (buddy, NULL, group, NULL);
    purple_account_add_buddy (conn->account, buddy);
}

gboolean
haze_contact_list_remove_from_group (HazeContactList *self,
    const gchar *group_name,
    TpHandle handle,
    GError **error)
{
    HazeConnection *conn = self->priv->conn;
    PurpleAccount *account = conn->account;
    const gchar *bname =
        haze_connection_handle_inspect (conn, TP_HANDLE_TYPE_CONTACT, handle);
    GSList *buddies;
    GSList *l;
    gboolean orphaned = TRUE;
    gboolean ret = TRUE;
    PurpleGroup *group = purple_find_group (group_name);

    g_return_val_if_fail (group != NULL, FALSE);

    buddies = purple_find_buddies (account, bname);

    for (l = buddies; l != NULL; l = l->next)
      {
        PurpleGroup *their_group = purple_buddy_get_group (l->data);

        if (their_group != group)
          {
            orphaned = FALSE;
            break;
          }
      }

    if (orphaned)
      {
        /* the contact needs to be copied to the default group first */
        PurpleGroup *default_group = purple_group_new (
            haze_get_fallback_group ());
        PurpleBuddy *copy;

        if (default_group == group)
          {
            /* we could make them bounce back, but that'd be insane */
            g_set_error (error, TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                "Contacts can't be removed from '%s' unless they are in "
                "another group", group->name);
            ret = FALSE;
            goto finally;
          }

        copy = purple_buddy_new (conn->account, bname, NULL);
        purple_blist_add_buddy (copy, NULL, default_group, NULL);
        purple_account_add_buddy (account, copy);
      }

    /* See if the buddy was in the group more than once, since this is
     * possible in libpurple... */
    for (l = buddies; l != NULL; l = l->next)
      {
        PurpleGroup *their_group = purple_buddy_get_group (l->data);

        if (their_group == group)
          {
            purple_account_remove_buddy (account, l->data, group);
            purple_blist_remove_buddy (l->data);
          }
      }

finally:
    g_slist_free (buddies);
    return ret;
}
