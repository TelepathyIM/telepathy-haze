#include <telepathy-glib/handle-repo-dynamic.h>
#include <telepathy-glib/handle-repo-static.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/errors.h>

#include "defines.h"
#include "connection.h"
#include "im-channel-factory.h"

enum
{
    PROP_USERNAME = 1,
    PROP_PASSWORD,
    PROP_SERVER,

    LAST_PROPERTY
};

G_DEFINE_TYPE_WITH_CODE(HazeConnection,
    haze_connection,
    TP_TYPE_BASE_CONNECTION,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_PRESENCE,
        tp_presence_mixin_iface_init);
    );

typedef struct _HazeConnectionPrivate
{
    char *username;
    char *password;
    char *server;
} HazeConnectionPrivate;

#define HAZE_CONNECTION_GET_PRIVATE(o) \
  ((HazeConnectionPrivate *)o->priv)

#define PC_GET_BASE_CONN(pc) \
    (TP_BASE_CONNECTION (purple_connection_get_account (pc)->ui_data))

void
signed_on_cb (PurpleConnection *pc, gpointer data)
{
    TpBaseConnection *base_conn = PC_GET_BASE_CONN (pc);

    tp_base_connection_change_status (base_conn,
        TP_CONNECTION_STATUS_CONNECTED,
        TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);
}

void
signing_off_cb (PurpleConnection *pc, gpointer data)
{
    TpBaseConnection *base_conn = PC_GET_BASE_CONN (pc);

    /* FIXME: reason for disconnection, via
     *        PurpleConnectionUiOps.report_disconnect I guess
     */
    if(base_conn->status != TP_CONNECTION_STATUS_DISCONNECTED)
    {
        tp_base_connection_change_status (base_conn,
            TP_CONNECTION_STATUS_DISCONNECTED,
            TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED);
    }
}

static gboolean
idle_signed_off_cb(gpointer data)
{
    PurpleAccount *account = (PurpleAccount *) data;
    HazeConnection *conn = ACCOUNT_GET_HAZE_CONNECTION (account);
    g_debug ("deleting account %s", account->username);
    purple_accounts_delete (account);
    tp_base_connection_finish_shutdown (TP_BASE_CONNECTION (conn));
    return FALSE;
}

void
signed_off_cb (PurpleConnection *pc, gpointer data)
{
    PurpleAccount *account = purple_connection_get_account (pc);
    g_idle_add(idle_signed_off_cb, account);
}

static gboolean
_haze_connection_start_connecting (TpBaseConnection *base,
                                   GError **error)
{
    HazeConnection *self = HAZE_CONNECTION(base);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE(self);
    char *protocol, *password, *prpl;
    TpHandleRepoIface *contact_handles =
        tp_base_connection_get_handles (base, TP_HANDLE_TYPE_CONTACT);

    g_object_get(G_OBJECT(self),
                 "protocol", &protocol,
                 "password", &password,
                 NULL);

    base->self_handle = tp_handle_ensure(contact_handles, priv->username,
                                         NULL, error);

    prpl = g_strconcat("prpl-", protocol, NULL);
    self->account = purple_account_new(priv->username, prpl);
    g_free(prpl);

    self->account->ui_data = self;
    purple_account_set_password(self->account, password);
    purple_account_set_enabled(self->account, UI_ID, TRUE);
    purple_account_connect(self->account);

    tp_base_connection_change_status(base, TP_CONNECTION_STATUS_CONNECTING,
                                     TP_CONNECTION_STATUS_REASON_REQUESTED);

    return TRUE;
}

static void
_haze_connection_shut_down (TpBaseConnection *base)
{
    HazeConnection *self = HAZE_CONNECTION(base);
    if(!self->account->disconnecting)
        purple_account_disconnect(self->account);
}

/* Must be in the same order as HazeListHandle in connection.h */
static const char *list_handle_strings[] =
{
    "subscribe",    /* HAZE_LIST_HANDLE_SUBSCRIBE */
#if 0
    "publish",      /* HAZE_LIST_HANDLE_PUBLISH */
    "known",        /* HAZE_LIST_HANDLE_KNOWN */
    "deny",         /* HAZE_LIST_HANDLE_DENY */
#endif
    NULL
};

static gchar*
_contact_normalize (TpHandleRepoIface *repo,
                    const gchar *id,
                    gpointer context,
                    GError **error)
{
    HazeConnection *conn = HAZE_CONNECTION (context);
    PurpleAccount *account = conn->account;
    return g_strdup (purple_normalize (account, id));
}

static void
_haze_connection_create_handle_repos (TpBaseConnection *base,
        TpHandleRepoIface *repos[NUM_TP_HANDLE_TYPES])
{
    repos[TP_HANDLE_TYPE_CONTACT] =
        tp_dynamic_handle_repo_new (TP_HANDLE_TYPE_CONTACT, _contact_normalize,
                                    base);
    /* repos[TP_HANDLE_TYPE_ROOM] = XXX MUC */
    repos[TP_HANDLE_TYPE_GROUP] =
        tp_dynamic_handle_repo_new (TP_HANDLE_TYPE_GROUP, NULL, NULL);
    repos[TP_HANDLE_TYPE_LIST] =
        tp_static_handle_repo_new (TP_HANDLE_TYPE_LIST, list_handle_strings);
}

static GPtrArray *
_haze_connection_create_channel_factories (TpBaseConnection *base)
{
    HazeConnection *self = HAZE_CONNECTION(base);
    GPtrArray *channel_factories = g_ptr_array_new ();

    self->im_factory = HAZE_IM_CHANNEL_FACTORY (
        g_object_new (HAZE_TYPE_IM_CHANNEL_FACTORY, "connection", self, NULL));
    g_ptr_array_add (channel_factories, self->im_factory);

    self->contact_list = HAZE_CONTACT_LIST (
        g_object_new (HAZE_TYPE_CONTACT_LIST, "connection", self, NULL));
    g_ptr_array_add (channel_factories, self->contact_list);

    return channel_factories;
}

gchar *
haze_connection_get_unique_connection_name(TpBaseConnection *base)
{
    HazeConnection *self = HAZE_CONNECTION(base);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE(self);

    return g_strdup(priv->username);
}

static void
haze_connection_get_property (GObject    *object,
                              guint       property_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    HazeConnection *self = HAZE_CONNECTION (object);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE(self);

    switch (property_id) {
        case PROP_USERNAME:
            g_value_set_string (value, priv->username);
            break;
        case PROP_PASSWORD:
            g_value_set_string (value, priv->password);
            break;
        case PROP_SERVER:
            g_value_set_string (value, priv->server);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
haze_connection_set_property (GObject      *object,
                              guint         property_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
    HazeConnection *self = HAZE_CONNECTION (object);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE(self);

    switch (property_id) {
        case PROP_USERNAME:
            g_free (priv->username);
            priv->username = g_value_dup_string(value);
            break;
        case PROP_PASSWORD:
            g_free (priv->password);
            priv->password = g_value_dup_string(value);
            break;
        case PROP_SERVER:
            g_free (priv->server);
            priv->server = g_value_dup_string(value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static GObject *
haze_connection_constructor (GType type,
                             guint n_construct_properties,
                             GObjectConstructParam *construct_params)
{
    HazeConnection *self = HAZE_CONNECTION (
            G_OBJECT_CLASS (haze_connection_parent_class)->constructor (
                type, n_construct_properties, construct_params));

    g_debug("Post-construction: (HazeConnection *)%p", self);


    return (GObject *)self;
}

static void
haze_connection_dispose (GObject *object)
{
    HazeConnection *self = HAZE_CONNECTION(object);

    g_debug("disposing of (HazeConnection *)%p", self);

    G_OBJECT_CLASS (haze_connection_parent_class)->dispose (object);
}

static void
haze_connection_finalize (GObject *object)
{
    HazeConnection *self = HAZE_CONNECTION (object);
    HazeConnectionPrivate *priv = HAZE_CONNECTION_GET_PRIVATE(self);

    g_free (priv->username);
    g_free (priv->password);
    g_free (priv->server);
    self->priv = NULL;

    tp_presence_mixin_finalize (object);

    G_OBJECT_CLASS (haze_connection_parent_class)->finalize (object);
}

static const TpPresenceStatusOptionalArgumentSpec arg_specs[] = {
    { "message", "s" },
    { NULL, NULL }
};

typedef enum {
    HAZE_STATUS_AVAILABLE = 0,
    HAZE_STATUS_BUSY,
    HAZE_STATUS_AWAY,
    HAZE_STATUS_EXT_AWAY,
    HAZE_STATUS_INVISIBLE,
    HAZE_STATUS_OFFLINE,

    HAZE_NUM_STATUSES
} HazeStatusIndex;

static const TpPresenceStatusSpec statuses[] = {
    { "available", TP_CONNECTION_PRESENCE_TYPE_AVAILABLE, TRUE,
        arg_specs, NULL, NULL },
    { "busy", TP_CONNECTION_PRESENCE_TYPE_AWAY, TRUE,
        arg_specs, NULL, NULL },
    { "away", TP_CONNECTION_PRESENCE_TYPE_AWAY, TRUE,
        arg_specs, NULL, NULL },
    { "ext_away", TP_CONNECTION_PRESENCE_TYPE_EXTENDED_AWAY, TRUE,
        arg_specs, NULL, NULL },
    { "invisible", TP_CONNECTION_PRESENCE_TYPE_HIDDEN, TRUE, NULL, NULL, NULL },
    { "offline", TP_CONNECTION_PRESENCE_TYPE_OFFLINE, FALSE, NULL, NULL, NULL },
    { NULL, TP_CONNECTION_PRESENCE_TYPE_UNSET, FALSE, NULL, NULL, NULL }
};

/* Indexed by HazeStatusIndex */
static const PurpleStatusPrimitive primitives[] = {
    PURPLE_STATUS_AVAILABLE,
    PURPLE_STATUS_UNAVAILABLE,
    PURPLE_STATUS_AWAY,
    PURPLE_STATUS_EXTENDED_AWAY,
    PURPLE_STATUS_INVISIBLE,
    PURPLE_STATUS_OFFLINE
};

/* Indexed by PurpleStatusPrimitive */
static const HazeStatusIndex status_indices[] = {
    HAZE_NUM_STATUSES,     /* invalid! */
    HAZE_STATUS_OFFLINE,   /* PURPLE_STATUS_OFFLINE */
    HAZE_STATUS_AVAILABLE, /* PURPLE_STATUS_AVAILABLE */
    HAZE_STATUS_BUSY,      /* PURPLE_STATUS_UNAVAILABLE */
    HAZE_STATUS_INVISIBLE, /* PURPLE_STATUS_INVISIBLE */
    HAZE_STATUS_AWAY,      /* PURPLE_STATUS_AWAY */
    HAZE_STATUS_EXT_AWAY   /* PURPLE_STATUS_EXTENDED_AWAY */
};

static TpPresenceStatus *
_get_tp_status (PurpleStatus *p_status)
{
    PurpleStatusType *type;
    PurpleStatusPrimitive prim;
    GHashTable *arguments = g_hash_table_new_full (g_str_hash, g_str_equal,
        NULL, (GDestroyNotify) tp_g_value_slice_free);
    guint status_ix = -1;
    const gchar *message;
    TpPresenceStatus *tp_status;

    g_assert (p_status != NULL);

    type = purple_status_get_type (p_status);
    prim = purple_status_type_get_primitive (type);
    status_ix = status_indices[prim];

    message = purple_status_get_attr_string (p_status, "message");
    if (message)
    {
        GValue *message_v = g_slice_new0 (GValue);
        g_value_init (message_v, G_TYPE_STRING);
        g_value_set_string (message_v, message);
        g_hash_table_insert (arguments, "message", message_v);
    }

    tp_status = tp_presence_status_new (status_ix, arguments);
    g_hash_table_destroy (arguments);
    return tp_status;
}

static const char *
_get_purple_status_id (HazeConnection *self,
                       guint index)
{
    PurpleStatusPrimitive prim = PURPLE_STATUS_UNSET;
    PurpleStatusType *type;

    g_assert (index < HAZE_NUM_STATUSES);
    prim = primitives[index];

    type = purple_account_get_status_type_with_primitive (self->account, prim);
    if (type)
    {
        return (purple_status_type_get_id (type));
    }
    else
    {
        return NULL;
    }
}

static gboolean
_status_available (GObject *obj,
                   guint index)
{
    HazeConnection *self = HAZE_CONNECTION (obj);
    /* FIXME: (a) should we be able to set offline on ourselves;
     *        (b) deal with some protocols not having status messages.
     */
    return (_get_purple_status_id (self, index) != NULL);
}


static GHashTable *
_get_contact_statuses (GObject *obj,
                       const GArray *contacts,
                       GError **error)
{
    GHashTable *status_table = g_hash_table_new_full (g_direct_hash,
        g_direct_equal, NULL, NULL);
    HazeConnection *conn = HAZE_CONNECTION (obj);
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (obj);
    TpHandleRepoIface *handle_repo =
        tp_base_connection_get_handles (base_conn, TP_HANDLE_TYPE_CONTACT);
    guint i;

    for (i = 0; i < contacts->len; i++)
    {
        TpHandle handle = g_array_index (contacts, TpHandle, i);
        const gchar *bname;
        TpPresenceStatus *tp_status;
        PurpleBuddy *buddy;
        PurpleStatus *p_status;

        g_assert (tp_handle_is_valid (handle_repo, handle, NULL));

        if (handle == base_conn->self_handle)
        {
            g_debug ("[%s] getting own status", conn->account->username);

            p_status = purple_account_get_active_status (conn->account);
        }
        else
        {
            bname = tp_handle_inspect (handle_repo, handle);
            g_debug ("[%s] getting status for %s",
                     conn->account->username, bname);
            buddy = purple_find_buddy (conn->account, bname);

            if (buddy)
            {
                PurplePresence *presence = purple_buddy_get_presence (buddy);
                p_status = purple_presence_get_active_status (presence);
            }
            else
            {
                g_critical ("can't find %s", bname);
                continue;
            }
        }

        tp_status = _get_tp_status (p_status);

        g_hash_table_insert (status_table, GINT_TO_POINTER (handle), tp_status);
    }

    return status_table;
}

static void
update_status (PurpleBuddy *buddy,
               PurpleStatus *status)
{
    PurpleAccount *account = purple_buddy_get_account (buddy);
    HazeConnection *conn = ACCOUNT_GET_HAZE_CONNECTION (account);
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
    TpHandleRepoIface *handle_repo =
        tp_base_connection_get_handles (base_conn, TP_HANDLE_TYPE_CONTACT);

    const gchar *bname = purple_buddy_get_name (buddy);
    TpHandle handle = tp_handle_ensure (handle_repo, bname, NULL, NULL);

    TpPresenceStatus *tp_status;

    g_debug ("%s changed to status %s", bname, purple_status_get_id (status));

    tp_status = _get_tp_status (status);

    g_debug ("tp_status index: %u", tp_status->index);

    tp_presence_mixin_emit_one_presence_update (G_OBJECT (conn), handle,
        tp_status);
    tp_handle_unref (handle_repo, handle);
}

static void
status_changed_cb (PurpleBuddy *buddy,
                   PurpleStatus *old_status,
                   PurpleStatus *new_status,
                   gpointer unused)
{
    update_status (buddy, new_status);
}

static void
signed_on_off_cb (PurpleBuddy *buddy,
                  gpointer data)
{
    /*
    gboolean signed_on = GPOINTER_TO_INT (data);
    */
    PurplePresence *presence = purple_buddy_get_presence (buddy);
    update_status (buddy, purple_presence_get_active_status (presence));
}

static gboolean
_set_own_status (GObject *obj,
                 const TpPresenceStatus *status,
                 GError **error)
{
    HazeConnection *self = HAZE_CONNECTION (obj);
    const char *status_id = NULL;
    GValue *message_v;
    char *message;
    GList *attrs = NULL;

    if (status)
        status_id = _get_purple_status_id (self, status->index);

    if (!status_id)
    {
        /* TODO: Is there a more sensible way to have a default? */
        g_debug ("defaulting to 'available' status");
        status_id = "available";
    }

    if (status->optional_arguments)
    {
        message_v = g_hash_table_lookup (status->optional_arguments, "message");
        if (message_v)
            message = g_value_dup_string (message_v);
    }

    if (message)
    {
        attrs = g_list_append (attrs, "message");
        attrs = g_list_append (attrs, message);
    }

    purple_account_set_status_list (self->account, status_id, TRUE, attrs);
    g_list_free (attrs);
    if (message)
        g_free (message);

    return TRUE;
}

static void
_init_presence (GObjectClass *object_class)
{
    tp_presence_mixin_class_init (object_class,
        G_STRUCT_OFFSET (HazeConnectionClass, presence_class),
        _status_available, _get_contact_statuses, _set_own_status, statuses);
}

static void
haze_connection_class_init (HazeConnectionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    TpBaseConnectionClass *base_class = TP_BASE_CONNECTION_CLASS (klass);
    GParamSpec *param_spec;
    static const gchar *interfaces_always_present[] = {
        TP_IFACE_CONNECTION_INTERFACE_PRESENCE,
        NULL };
    void *connection_handle = purple_connections_get_handle ();
    void *blist_handle = purple_blist_get_handle ();

    g_debug("Initializing (HazeConnectionClass *)%p", klass);

    g_type_class_add_private (klass, sizeof (HazeConnectionPrivate));
    object_class->get_property = haze_connection_get_property;
    object_class->set_property = haze_connection_set_property;
    object_class->constructor = haze_connection_constructor;
    object_class->dispose = haze_connection_dispose;
    object_class->finalize = haze_connection_finalize;

    base_class->create_handle_repos = _haze_connection_create_handle_repos;
    base_class->create_channel_factories =
        _haze_connection_create_channel_factories;
    base_class->get_unique_connection_name =
        haze_connection_get_unique_connection_name;
    base_class->start_connecting = _haze_connection_start_connecting;
    base_class->shut_down = _haze_connection_shut_down;
    base_class->interfaces_always_present = interfaces_always_present;

    param_spec = g_param_spec_string ("username", "Account username",
                                      "The username used when authenticating.",
                                      NULL,
                                      G_PARAM_CONSTRUCT_ONLY |
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NAME |
                                      G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_USERNAME, param_spec);

    param_spec = g_param_spec_string ("password", "Account password",
                                      "The password used when authenticating.",
                                      NULL,
                                      G_PARAM_CONSTRUCT_ONLY |
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NAME |
                                      G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_PASSWORD, param_spec);

    param_spec = g_param_spec_string ("server", "Hostname or IP of server",
                                      "The server used when establishing a connection.",
                                      NULL,
                                      G_PARAM_CONSTRUCT_ONLY |
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NAME |
                                      G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_SERVER, param_spec);

    purple_signal_connect(connection_handle, "signed-on",
                          klass, PURPLE_CALLBACK(signed_on_cb), NULL);
    purple_signal_connect(connection_handle, "signing-off",
                          klass, PURPLE_CALLBACK(signing_off_cb), NULL);
    purple_signal_connect(connection_handle, "signed-off",
                          klass, PURPLE_CALLBACK(signed_off_cb), NULL);

    purple_signal_connect (blist_handle, "buddy-status-changed", klass,
        PURPLE_CALLBACK (status_changed_cb), NULL);
    purple_signal_connect (blist_handle, "buddy-signed-on", klass,
        PURPLE_CALLBACK (signed_on_off_cb), GINT_TO_POINTER (TRUE));
    purple_signal_connect (blist_handle, "buddy-signed-off", klass,
        PURPLE_CALLBACK (signed_on_off_cb), GINT_TO_POINTER (FALSE));

    _init_presence (object_class);
}

static void
haze_connection_init (HazeConnection *self)
{
    g_debug("Initializing (HazeConnection *)%p", self);
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, HAZE_TYPE_CONNECTION,
                                              HazeConnectionPrivate);

    tp_presence_mixin_init (G_OBJECT (self), G_STRUCT_OFFSET (HazeConnection,
        presence));
}
