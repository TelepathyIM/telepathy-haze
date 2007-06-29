#include <string.h>

#include <telepathy-glib/channel-factory-iface.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/handle-repo.h>
#include <telepathy-glib/base-connection.h>

#include "im-channel.h"

#include "im-channel-factory.h"
#include "connection.h"

typedef struct _HazeImChannelFactoryPrivate HazeImChannelFactoryPrivate;
struct _HazeImChannelFactoryPrivate {
    HazeConnection *conn;
    GHashTable *channels;
    gboolean dispose_has_run;
};
#define HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE(o) \
    (G_TYPE_INSTANCE_GET_PRIVATE((o), HAZE_TYPE_IM_CHANNEL_FACTORY, \
                                 HazeImChannelFactoryPrivate))

static void haze_im_channel_factory_iface_init (gpointer g_iface,
                                                gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE(HazeImChannelFactory,
    haze_im_channel_factory,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_FACTORY_IFACE,
      haze_im_channel_factory_iface_init));

/* properties: */
enum {
    PROP_CONNECTION = 1,
    
    LAST_PROPERTY
};

static void
haze_im_channel_factory_init (HazeImChannelFactory *self)
{
    HazeImChannelFactoryPrivate *priv =
        HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE(self);

    priv->channels = g_hash_table_new_full (g_direct_hash, g_direct_equal,
                                            NULL, g_object_unref);

    priv->conn = NULL;
    priv->dispose_has_run = FALSE;
}

static void
haze_im_channel_factory_dispose (GObject *object)
{
    HazeImChannelFactory *self = HAZE_IM_CHANNEL_FACTORY (object);
    HazeImChannelFactoryPrivate *priv =
        HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE(self);

    if (priv->dispose_has_run)
        return;

    priv->dispose_has_run = TRUE;

    tp_channel_factory_iface_close_all (TP_CHANNEL_FACTORY_IFACE (object));
    g_assert (priv->channels == NULL);

    if (G_OBJECT_CLASS (haze_im_channel_factory_parent_class)->dispose)
        G_OBJECT_CLASS (haze_im_channel_factory_parent_class)->dispose (object);
}

static void
haze_im_channel_factory_get_property (GObject    *object,
                                 guint       property_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
    HazeImChannelFactory *fac = HAZE_IM_CHANNEL_FACTORY (object);
    HazeImChannelFactoryPrivate *priv =
        HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE (fac);

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
haze_im_channel_factory_set_property (GObject      *object,
                                 guint         property_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
    HazeImChannelFactory *fac = HAZE_IM_CHANNEL_FACTORY (object);
    HazeImChannelFactoryPrivate *priv =
        HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE (fac);

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
haze_im_channel_factory_class_init (HazeImChannelFactoryClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GParamSpec *param_spec;

    object_class->dispose = haze_im_channel_factory_dispose;
    object_class->get_property = haze_im_channel_factory_get_property;
    object_class->set_property = haze_im_channel_factory_set_property;

    param_spec = g_param_spec_object ("connection", "HazeConnection object",
                                      "Hazee connection object that owns this "
                                      "IM channel factory object.",
                                      HAZE_TYPE_CONNECTION,
                                      G_PARAM_CONSTRUCT_ONLY |
                                      G_PARAM_READWRITE |
                                      G_PARAM_STATIC_NICK |
                                      G_PARAM_STATIC_BLURB);
    g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

    g_type_class_add_private (object_class,
                              sizeof(HazeImChannelFactoryPrivate));
}

static HazeIMChannel *
new_im_channel (HazeImChannelFactory *self,
                guint handle)
{
    HazeImChannelFactoryPrivate *priv;
    TpBaseConnection *conn;
    HazeIMChannel *chan;
    char *object_path;

    g_assert (HAZE_IS_IM_CHANNEL_FACTORY (self));

    priv = HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE (self);
    conn = (TpBaseConnection *)priv->conn;

    object_path = g_strdup_printf ("%s/ImChannel%u", conn->object_path, handle);

    chan = g_object_new (HAZE_TYPE_IM_CHANNEL,
                         "connection", priv->conn,
                         "object-path", object_path,
                         "handle", handle,
                         NULL);

    g_debug ("object path %s", object_path);
// XXX
//    g_signal_connect (chan, "closed", (GCallback) im_channel_closed_cb, self);

    g_hash_table_insert (priv->channels, GINT_TO_POINTER (handle), chan);

    tp_channel_factory_iface_emit_new_channel (self, (TpChannelIface *)chan,
            NULL);

    g_free (object_path);

    return chan;
}

static void
haze_im_channel_factory_iface_close_all (TpChannelFactoryIface *iface)
{
    HazeImChannelFactory *fac = HAZE_IM_CHANNEL_FACTORY (iface);
    HazeImChannelFactoryPrivate *priv =
        HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE (fac);

    // XXX do things!
    g_warning ("yaaaaaaaaaargh leaking priv->channels in close_all(%p)!",
               fac);

    priv->channels = NULL;
}

static void
received_message_cb(PurpleAccount *account,
                    const char *sender,
                    char *message,
                    PurpleConversation *conv,
                    PurpleMessageFlags flags,
                    HazeImChannelFactory *self)
{
    HazeImChannelFactoryPrivate *priv =
        HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE (self);
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->conn);
    TpHandleRepoIface *contact_repo =
        tp_base_connection_get_handles (base_conn, TP_HANDLE_TYPE_CONTACT);
    TpHandle handle;
    HazeIMChannel *chan = NULL;

    if(priv->conn->account != account)
        return;
    
    handle = tp_handle_ensure (contact_repo, sender, NULL, NULL);
    if (handle == 0) {
        g_debug ("got a 0 handle, ignoring message");
        return;
    }

    chan = g_hash_table_lookup (priv->channels, GINT_TO_POINTER (handle));
    if (chan == NULL) {
        g_debug ("creating a new channel...");
        chan = new_im_channel (self, handle);
    }
    g_assert (chan != NULL);
    
    tp_handle_unref (contact_repo, handle); /* reffed by chan */

    tp_text_mixin_receive (G_OBJECT (chan),
                           TP_CHANNEL_TEXT_MESSAGE_TYPE_NORMAL, handle,
                           time (NULL), message); // XXX timestamp!
}

static void
haze_im_channel_factory_iface_connecting (TpChannelFactoryIface *iface)
{
    HazeImChannelFactory *self = HAZE_IM_CHANNEL_FACTORY (iface);
    void *conv_handle = purple_conversations_get_handle();
    purple_signal_connect(conv_handle, "received-im-msg", self,
                          PURPLE_CALLBACK(received_message_cb), self);
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
haze_im_channel_factory_iface_foreach (TpChannelFactoryIface *iface,
                                       TpChannelFunc foreach,
                                       gpointer user_data)
{
    HazeImChannelFactory *fac = HAZE_IM_CHANNEL_FACTORY (iface);
    HazeImChannelFactoryPrivate *priv =
        HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE (fac);
    struct _ForeachData data;

    data.user_data = user_data;
    data.foreach = foreach;

    g_hash_table_foreach (priv->channels, _foreach_slave, &data);
}

static TpChannelFactoryRequestStatus
haze_im_channel_factory_iface_request (TpChannelFactoryIface *iface,
                                       const gchar *chan_type,
                                       TpHandleType handle_type,
                                       guint handle,
                                       gpointer request,
                                       TpChannelIface **ret,
                                       GError **error)
{
    HazeImChannelFactory *self = HAZE_IM_CHANNEL_FACTORY (iface);
    HazeImChannelFactoryPrivate *priv =
        HAZE_IM_CHANNEL_FACTORY_GET_PRIVATE (self);
    TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
            (TpBaseConnection *)priv->conn, TP_HANDLE_TYPE_CONTACT);
    HazeIMChannel *chan;
    TpChannelFactoryRequestStatus status;

    if (strcmp (chan_type, TP_IFACE_CHANNEL_TYPE_TEXT))
        return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_IMPLEMENTED;

    if (handle_type != TP_HANDLE_TYPE_CONTACT)
        return TP_CHANNEL_FACTORY_REQUEST_STATUS_NOT_AVAILABLE;

    if (!tp_handle_is_valid (contact_repo, handle, error))
        return TP_CHANNEL_FACTORY_REQUEST_STATUS_ERROR;

    chan = g_hash_table_lookup (priv->channels, GINT_TO_POINTER (handle));

    if (chan)
    {
        status = TP_CHANNEL_FACTORY_REQUEST_STATUS_EXISTING;
    }
    else
    {
        status = TP_CHANNEL_FACTORY_REQUEST_STATUS_CREATED;
        chan = new_im_channel (self, handle);
    }

    g_assert (chan);
    *ret = TP_CHANNEL_IFACE (chan);
    return status;
}

static void
haze_im_channel_factory_iface_init (gpointer g_iface,
                                    gpointer iface_data)
{
    TpChannelFactoryIfaceClass *klass = (TpChannelFactoryIfaceClass *) g_iface;

    klass->close_all = haze_im_channel_factory_iface_close_all;
    klass->connecting = haze_im_channel_factory_iface_connecting;
    klass->connected = NULL; //haze_im_channel_factory_iface_connected;
    klass->disconnected = NULL; //haze_im_channel_factory_iface_disconnected;
    klass->foreach = haze_im_channel_factory_iface_foreach;
    klass->request = haze_im_channel_factory_iface_request;
}
