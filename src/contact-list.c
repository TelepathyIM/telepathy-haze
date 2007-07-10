#include <string.h>

#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/intset.h>

#include "connection.h"
#include "contact-list.h"
#include "contact-list-channel.h"

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
}

static HazeContactListChannel *
_haze_contact_list_create_channel (HazeContactList *contact_list,
                                   guint handle_type,
                                   TpHandle handle)
{
    HazeContactListPrivate *priv = HAZE_CONTACT_LIST_GET_PRIVATE(contact_list);
    TpBaseConnection *conn = TP_BASE_CONNECTION (priv->conn);
    TpHandleRepoIface *handle_repo =
        tp_base_connection_get_handles (conn, handle_type);
    HazeContactListChannel *chan;
    const char *name;
    char *mangled_name;
    char *object_path;
    GHashTable *channels = (handle_type == TP_HANDLE_TYPE_LIST
                           ? priv->list_channels
                           : NULL); // XXX priv->group_channels);

    /* if this assertion succeeds, we know we have the right handle repo */
    g_assert (handle_type == TP_HANDLE_TYPE_LIST ||
            handle_type == TP_HANDLE_TYPE_GROUP);
    g_assert (channels != NULL);
    g_assert (g_hash_table_lookup (channels, GINT_TO_POINTER (handle)) == NULL);

    name = tp_handle_inspect (handle_repo, handle);
    g_debug ("Instantiating channel %u:%u \"%s\"", handle_type, handle, name);
    mangled_name = tp_escape_as_identifier (name);
    object_path = g_strdup_printf ("%s/ContactListChannel/%s/%s",
                                   conn->object_path,
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

    g_debug ("created %s", object_path);

    g_hash_table_insert (channels, GINT_TO_POINTER (handle), chan);

    tp_channel_factory_iface_emit_new_channel (contact_list,
                                               (TpChannelIface *)chan, NULL);
    g_free (object_path);

    return chan;
}

static void
haze_contact_list_factory_iface_close_all (TpChannelFactoryIface *iface)
{
    HazeContactList *self = HAZE_CONTACT_LIST (iface);
    HazeContactListPrivate *priv = HAZE_CONTACT_LIST_GET_PRIVATE (self);

    if (priv->list_channels)
    {
        g_hash_table_destroy (priv->list_channels);
        priv->list_channels = NULL;
    }
    if (priv->group_channels)
    {
        g_hash_table_destroy (priv->group_channels);
        priv->group_channels = NULL;
    }
}

static void
haze_contact_list_factory_iface_connecting (TpChannelFactoryIface *iface)
{
    /* XXX */
}

typedef struct _HandleContext {
    TpHandleRepoIface *contact_repo;
    TpHandleSet *set;
} HandleContext;

static void
_add_buddy_to_handle_set (PurpleBuddy *buddy,
                          HandleContext *context)
{
    const gchar *name = purple_buddy_get_name (buddy);
    TpHandle handle = tp_handle_ensure (context->contact_repo, name,
        NULL, NULL);
    tp_handle_set_add (context->set, handle);
    tp_handle_unref (context->contact_repo, handle); /* reffed by set */
}

static TpHandleSet *
_handle_my_buddies (HazeConnection *conn,
                    GSList *buddies)
{
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
    TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (base_conn,
        TP_HANDLE_TYPE_CONTACT);
    TpHandleSet *handles = tp_handle_set_new (contact_repo);
    HandleContext context = { contact_repo, handles };

    g_slist_foreach (buddies, (GFunc) _add_buddy_to_handle_set, &context);

    return handles;
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
buddy_added_cb (PurpleBuddy *buddy, gpointer data)
{
    HazeContactList *contact_list = HAZE_CONTACT_LIST (data);
    HazeContactListPrivate *priv = HAZE_CONTACT_LIST_GET_PRIVATE (contact_list);
    HazeContactListChannel *subscribe;
    TpHandleSet *add_handles;

    if (buddy->account != priv->conn->account)
        return;

    g_debug ("buddy_added_cb (%s)", purple_buddy_get_name (buddy));

    add_handles = _handle_a_buddy (priv->conn, buddy);

    subscribe = g_hash_table_lookup (priv->list_channels,
        GINT_TO_POINTER (HAZE_LIST_HANDLE_SUBSCRIBE));

    tp_group_mixin_change_members (G_OBJECT (subscribe), "",
        tp_handle_set_peek (add_handles), NULL, NULL, NULL, 0, 0);

    tp_handle_set_destroy (add_handles);
}

static void
buddy_removed_cb (PurpleBuddy *buddy, gpointer data)
{
    HazeContactList *contact_list = HAZE_CONTACT_LIST (data);
    HazeContactListPrivate *priv = HAZE_CONTACT_LIST_GET_PRIVATE (contact_list);
    HazeContactListChannel *subscribe;
    TpHandleSet *rem_handles;

    if (buddy->account != priv->conn->account)
        return;

    g_debug ("buddy_removed_cb (%s)", purple_buddy_get_name (buddy));

    rem_handles = _handle_a_buddy (priv->conn, buddy);

    subscribe = g_hash_table_lookup (priv->list_channels,
        GINT_TO_POINTER (HAZE_LIST_HANDLE_SUBSCRIBE));

    tp_group_mixin_change_members (G_OBJECT (subscribe), "",
        NULL, tp_handle_set_peek (rem_handles), NULL, NULL, 0, 0);

    tp_handle_set_destroy (rem_handles);
}

static void
haze_contact_list_factory_iface_connected (TpChannelFactoryIface *iface)
{
    HazeContactList *self = HAZE_CONTACT_LIST (iface);
    HazeContactListPrivate *priv = HAZE_CONTACT_LIST_GET_PRIVATE (self);
    HazeContactListChannel *subscribe;
    TpHandleSet *add_handles;

    PurpleAccount *account = priv->conn->account;

    GSList *buddies = purple_find_buddies(account, NULL);
    add_handles = _handle_my_buddies (priv->conn, buddies);
    g_slist_free (buddies);
    
    subscribe = _haze_contact_list_create_channel (self, TP_HANDLE_TYPE_LIST,
                                                   HAZE_LIST_HANDLE_SUBSCRIBE);

    tp_group_mixin_change_members (G_OBJECT (subscribe), "",
        tp_handle_set_peek (add_handles), NULL, NULL, NULL, 0, 0);

    tp_handle_set_destroy (add_handles);

    purple_signal_connect (purple_blist_get_handle(), "buddy-added",
                           self, PURPLE_CALLBACK(buddy_added_cb), self);
    purple_signal_connect (purple_blist_get_handle(), "buddy-removed",
                           self, PURPLE_CALLBACK(buddy_removed_cb), self);
    /* XXX */
}

static void
haze_contact_list_factory_iface_disconnected (TpChannelFactoryIface *iface)
{
    purple_signals_disconnect_by_handle (iface);
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
/*
    HazeContactList *self = HAZE_CONTACT_LIST (iface);
    HazeContactListPrivate *priv = HAZE_CONTACT_LIST_GET_PRIVATE (self);
*/

    if (strcmp (chan_type, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST))
        return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;

    if (handle_type != TP_HANDLE_TYPE_LIST
/*        && handle_type != TP_HANDLE_TYPE_GROUP*/
       )
    {
        return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;
    }
    
    /* XXX */

    return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;
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
