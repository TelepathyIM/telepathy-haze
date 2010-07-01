/*
 * contact-list-channel.c - HazeContactListChannel source
 * Copyright (C) 2007 Will Thompson
 * Copyright (C) 2007-2008 Collabora Ltd.
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

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/exportable-channel.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/channel-iface.h>
#include <telepathy-glib/svc-generic.h>

#include "contact-list-channel.h"
#include "connection.h"
#include "debug.h"

struct _HazeContactListChannelPrivate {
    HazeConnection *conn;

    char *object_path;
    TpHandle handle;
    guint handle_type;

    /* Defined only if handle_type == TP_HANDLE_TYPE_GROUP; NULL otherwise.
     */
    PurpleGroup *group;

    gboolean closed;
    gboolean dispose_has_run;
};

static void channel_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (HazeContactListChannel,
    haze_contact_list_channel,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL, channel_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CHANNEL_IFACE, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_TYPE_CONTACT_LIST, NULL);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CHANNEL_INTERFACE_GROUP,
        tp_group_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
        tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_EXPORTABLE_CHANNEL, NULL))

const char *haze_contact_list_channel_interfaces[] = {
    TP_IFACE_CHANNEL_INTERFACE_GROUP,
    NULL
};


/* properties: */
enum {
    PROP_CONNECTION = 1,
    PROP_OBJECT_PATH,
    PROP_CHANNEL_TYPE,
    PROP_HANDLE_TYPE,
    PROP_HANDLE,
    PROP_TARGET_ID,
    PROP_INTERFACES,
    PROP_REQUESTED,
    PROP_INITIATOR_HANDLE,
    PROP_INITIATOR_ID,
    PROP_CHANNEL_PROPERTIES,
    PROP_CHANNEL_DESTROYED,

    LAST_PROPERTY
};


static gboolean
_list_add_member_cb (HazeContactListChannel *chan,
                     TpHandle handle,
                     const gchar *message,
                     GError **error)
{
    HazeContactListChannelPrivate *priv =
        chan->priv;
    HazeConnection *conn = priv->conn;
    const gchar *bname =
        haze_connection_handle_inspect (conn, TP_HANDLE_TYPE_CONTACT, handle);
    PurpleBuddy *buddy;

    g_assert (priv->handle_type == TP_HANDLE_TYPE_LIST);

    switch (priv->handle)
    {
        case HAZE_LIST_HANDLE_SUBSCRIBE:
            /* If the buddy already exists, then it should already be on the
             * subscribe list.
             */
            g_assert (purple_find_buddy (conn->account, bname) == NULL);

            buddy = purple_buddy_new (conn->account, bname, NULL);

            /* FIXME: This emits buddy-added at once, so a buddy will never be
             * on the pending list.  It doesn't look like libpurple even has
             * the concept of a pending buddy.  Sigh.
             */
            purple_blist_add_buddy(buddy, NULL, NULL, NULL);
            purple_account_add_buddy(buddy->account, buddy);

            return TRUE; /* FIXME: How am I meant to know if it failed? */

        case HAZE_LIST_HANDLE_PUBLISH:
            haze_contact_list_accept_publish_request (conn->contact_list,
                handle);
            return TRUE;

        default:
            /* TODO: more list types */
            g_assert_not_reached ();
            return FALSE;
    }
}

static gboolean
_group_add_member_cb (HazeContactListChannel *chan,
                      TpHandle handle,
                      const gchar *message,
                      GError **error)
{
    HazeContactListChannelPrivate *priv =
        chan->priv;
    HazeConnection *conn = priv->conn;
    const gchar *bname =
        haze_connection_handle_inspect (conn, TP_HANDLE_TYPE_CONTACT, handle);
    PurpleBuddy *buddy;

    g_assert (priv->handle_type == TP_HANDLE_TYPE_GROUP);

    /* If the buddy is already in this group then this callback should
     * never have been called.
     */
    g_assert (purple_find_buddy_in_group (conn->account, bname,
        priv->group) == NULL);

    buddy = purple_buddy_new (conn->account, bname, NULL);

    /* FIXME: This causes it to be added to 'subscribed' too. */
    purple_blist_add_buddy (buddy, NULL, priv->group, NULL);
    purple_account_add_buddy(buddy->account, buddy);

    return TRUE;
}

static gboolean
_haze_contact_list_channel_add_member_cb (GObject *obj,
                                          TpHandle handle,
                                          const gchar *message,
                                          GError **error)
{
    HazeContactListChannel *chan = HAZE_CONTACT_LIST_CHANNEL (obj);
    HazeContactListChannelPrivate *priv =
        chan->priv;
    switch (priv->handle_type)
    {
        case TP_HANDLE_TYPE_LIST:
            return _list_add_member_cb (chan, handle, message, error);
        case TP_HANDLE_TYPE_GROUP:
            return _group_add_member_cb (chan, handle, message, error);
        default:
            g_assert_not_reached ();
            return FALSE;
    }
}

static gboolean
_list_remove_member_cb (HazeContactListChannel *chan,
                        TpHandle handle,
                        const gchar *message,
                        GError **error)
{
    HazeContactListChannelPrivate *priv =
        chan->priv;
    HazeConnection *conn = priv->conn;
    PurpleAccount *account = conn->account;
    const gchar *bname =
        haze_connection_handle_inspect (conn, TP_HANDLE_TYPE_CONTACT, handle);
    GSList *buddies, *l;
    PurpleBuddy *buddy;
    PurpleGroup *group;

    switch (priv->handle)
    {
        case HAZE_LIST_HANDLE_SUBSCRIBE:
            buddies = purple_find_buddies (account, bname);

            if (!buddies)
            {
                g_warning("'%s' is in the group mixin for '%s' but not on the "
                        "libpurple blist", bname, account->username);
                /* This occurring is a bug in haze or libpurple, but I figure
                 * it's better not to explode
                 */
                return TRUE;
            }

            /* Removing a buddy from subscribe entails removing it from all
             * groups since you can't have a buddy without groups in libpurple.
             */
            for (l = buddies; l != NULL; l = l->next)
            {
                buddy = (PurpleBuddy *) l->data;
                group = purple_buddy_get_group (buddy);
                purple_account_remove_buddy (account, buddy, group);
                purple_blist_remove_buddy (buddy);
            }

            g_slist_free (buddies);

            return TRUE;

        case HAZE_LIST_HANDLE_PUBLISH:
            haze_contact_list_reject_publish_request (conn->contact_list,
                handle);
            return TRUE;

        default:
            /* TODO: More list types */
            g_assert_not_reached ();
            return FALSE;
    }
}

static gboolean
_group_remove_member_cb (HazeContactListChannel *chan,
                         TpHandle handle,
                         const gchar *message,
                         GError **error)
{
    HazeContactListChannelPrivate *priv =
        chan->priv;
    HazeConnection *conn = priv->conn;
    PurpleAccount *account = conn->account;
    const gchar *bname =
        haze_connection_handle_inspect (conn, TP_HANDLE_TYPE_CONTACT, handle);
    PurpleGroup *group = priv->group;
    GSList *buddies = purple_find_buddies (account, bname);
    GSList *l;
    gboolean orphaned = TRUE;
    gboolean ret = TRUE;

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

static gboolean
_haze_contact_list_channel_remove_member_cb (GObject *obj,
                                             TpHandle handle,
                                             const gchar *message,
                                             GError **error)
{
    HazeContactListChannel *chan = HAZE_CONTACT_LIST_CHANNEL (obj);
    HazeContactListChannelPrivate *priv =
        chan->priv;

    switch (priv->handle_type)
    {
        case TP_HANDLE_TYPE_LIST:
            return _list_remove_member_cb (chan, handle, message, error);
        case TP_HANDLE_TYPE_GROUP:
            return _group_remove_member_cb (chan, handle, message, error);
        default:
            g_assert_not_reached ();
            return FALSE;
    }
}


static void
haze_contact_list_channel_init (HazeContactListChannel *self)
{
    HazeContactListChannelPrivate *priv =
        (G_TYPE_INSTANCE_GET_PRIVATE((self), HAZE_TYPE_CONTACT_LIST_CHANNEL,
                                     HazeContactListChannelPrivate));

    self->priv = priv;
    priv->dispose_has_run = FALSE;
    priv->closed = FALSE;
}

static GObject *
haze_contact_list_channel_constructor (GType type, guint n_props,
                                       GObjectConstructParam *props)
{
    GObject *obj;
    HazeContactListChannel *chan;
    HazeContactListChannelPrivate *priv;
    TpBaseConnection *conn;
    TpHandle self_handle;
    TpHandleRepoIface *handle_repo, *contact_repo;
    DBusGConnection *bus;
    guint handle_type;

    obj = G_OBJECT_CLASS (haze_contact_list_channel_parent_class)->
        constructor (type, n_props, props);
    chan = HAZE_CONTACT_LIST_CHANNEL (obj);
    priv = chan->priv;
    conn = TP_BASE_CONNECTION (priv->conn);
    self_handle = conn->self_handle;
    handle_type = priv->handle_type;
    handle_repo = tp_base_connection_get_handles (conn, handle_type);
    contact_repo = tp_base_connection_get_handles (conn,
                                                   TP_HANDLE_TYPE_CONTACT);

    bus = tp_get_bus ();
    dbus_g_connection_register_g_object (bus, priv->object_path, obj);

    g_assert (handle_type == TP_HANDLE_TYPE_LIST ||
              handle_type == TP_HANDLE_TYPE_GROUP);

    tp_handle_ref (handle_repo, priv->handle);

    tp_group_mixin_init (obj, G_STRUCT_OFFSET (HazeContactListChannel, group),
                         contact_repo, self_handle);

    tp_group_mixin_change_flags (obj, TP_CHANNEL_GROUP_FLAG_PROPERTIES, 0);

    switch (handle_type) {
        case TP_HANDLE_TYPE_GROUP:
            tp_group_mixin_change_flags (obj,
                    TP_CHANNEL_GROUP_FLAG_CAN_ADD |
                    TP_CHANNEL_GROUP_FLAG_CAN_REMOVE,
                    0);

            priv->group = purple_group_new (tp_handle_inspect (handle_repo,
                                                               priv->handle));
            break;
        case TP_HANDLE_TYPE_LIST:
            switch (priv->handle) {
                case HAZE_LIST_HANDLE_SUBSCRIBE:
                    tp_group_mixin_change_flags (obj,
                            TP_CHANNEL_GROUP_FLAG_CAN_ADD |
                            TP_CHANNEL_GROUP_FLAG_CAN_REMOVE |
                            TP_CHANNEL_GROUP_FLAG_CAN_RESCIND,
                            0);
                    break;
                case HAZE_LIST_HANDLE_PUBLISH:
                    /* This is kind of a fake publish channel: it's only used
                     * to accept or reject incoming subscription requests.  So,
                     * you can't add or remove anyone from it yourself; people
                     * get onto local_pending when they make a subscribe
                     * request, and then you can move them to the list proper
                     * or remove them.
                     *
                     * So, we'll leave the channel's flags as 0.
                     */
                    break;
                /* TODO: More magic lists go here */
                default:
                    g_assert_not_reached ();
            }
            break;
        default:
            g_assert_not_reached ();
    }

    return obj;
}

static void
haze_contact_list_channel_dispose (GObject *object)
{
    HazeContactListChannel *self = HAZE_CONTACT_LIST_CHANNEL (object);
    HazeContactListChannelPrivate *priv = self->priv;

    if (priv->dispose_has_run)
        return;

    priv->dispose_has_run = TRUE;

    if (!priv->closed)
    {
        priv->closed = TRUE;
        tp_svc_channel_emit_closed ((TpSvcChannel *)self);
    }

    g_free (priv->object_path);

    if (G_OBJECT_CLASS (haze_contact_list_channel_parent_class)->dispose)
        G_OBJECT_CLASS (haze_contact_list_channel_parent_class)->dispose (object);
}

static void
haze_contact_list_channel_finalize (GObject *object)
{
    tp_group_mixin_finalize (object);

    G_OBJECT_CLASS (haze_contact_list_channel_parent_class)->finalize (object);
}

static void
haze_contact_list_channel_get_property (GObject    *object,
                                        guint       property_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
    HazeContactListChannel *self = HAZE_CONTACT_LIST_CHANNEL (object);
    HazeContactListChannelPrivate *priv = self->priv;
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (priv->conn);

    switch (property_id) {
        case PROP_OBJECT_PATH:
            g_value_set_string (value, priv->object_path);
            break;
        case PROP_CHANNEL_TYPE:
            g_value_set_static_string (value, TP_IFACE_CHANNEL_TYPE_CONTACT_LIST);
            break;
        case PROP_HANDLE_TYPE:
            g_value_set_uint (value, priv->handle_type);
            break;
        case PROP_HANDLE:
            g_value_set_uint (value, priv->handle);
            break;
        case PROP_TARGET_ID:
        {
            TpHandleRepoIface *repo = tp_base_connection_get_handles (base_conn,
                priv->handle_type);

            g_value_set_string (value, tp_handle_inspect (repo, priv->handle));
            break;
        }
        case PROP_INITIATOR_HANDLE:
            g_value_set_uint (value, 0);
            break;
        case PROP_INITIATOR_ID:
            g_value_set_static_string (value, "");
            break;
        case PROP_REQUESTED:
            g_value_set_boolean (value, FALSE);
            break;
        case PROP_CONNECTION:
            g_value_set_object (value, priv->conn);
            break;
        case PROP_INTERFACES:
            g_value_set_boxed (value, haze_contact_list_channel_interfaces);
            break;
        case PROP_CHANNEL_DESTROYED:
            g_value_set_boolean (value, priv->closed);
            break;
        case PROP_CHANNEL_PROPERTIES:
            g_value_take_boxed (value,
                tp_dbus_properties_mixin_make_properties_hash (object,
                    TP_IFACE_CHANNEL, "TargetHandle",
                    TP_IFACE_CHANNEL, "TargetHandleType",
                    TP_IFACE_CHANNEL, "ChannelType",
                    TP_IFACE_CHANNEL, "TargetID",
                    TP_IFACE_CHANNEL, "InitiatorHandle",
                    TP_IFACE_CHANNEL, "InitiatorID",
                    TP_IFACE_CHANNEL, "Requested",
                    TP_IFACE_CHANNEL, "Interfaces",
                    NULL));
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
haze_contact_list_channel_set_property (GObject      *object,
                                        guint         property_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
    HazeContactListChannel *self = HAZE_CONTACT_LIST_CHANNEL (object);
    HazeContactListChannelPrivate *priv = self->priv;

    switch (property_id) {
        case PROP_OBJECT_PATH:
            g_free (priv->object_path);
            priv->object_path = g_value_dup_string (value);
            break;
        case PROP_CHANNEL_TYPE:
          /* this property is writable in the interface (in
           * telepathy-glib > 0.7.0), but not actually
           * meaningfully changeable on this channel, so we do nothing */
          break;
        case PROP_HANDLE_TYPE:
            priv->handle_type = g_value_get_uint (value);
            break;
        case PROP_HANDLE:
            priv->handle = g_value_get_uint (value);
            break;
        case PROP_CONNECTION:
            priv->conn = g_value_get_object (value);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
            break;
    }
}

static void
haze_contact_list_channel_class_init (HazeContactListChannelClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GParamSpec *param_spec;

    static gboolean properties_mixin_initialized = FALSE;
    static TpDBusPropertiesMixinPropImpl channel_props[] = {
        { "TargetHandleType", "handle-type", NULL },
        { "TargetHandle", "handle", NULL },
        { "TargetID", "target-id", NULL },
        { "ChannelType", "channel-type", NULL },
        { "Interfaces", "interfaces", NULL },
        { "Requested", "requested", NULL },
        { "InitiatorHandle", "initiator-handle", NULL },
        { "InitiatorID", "initiator-id", NULL },
        { NULL }
    };
    static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
        { TP_IFACE_CHANNEL,
          tp_dbus_properties_mixin_getter_gobject_properties,
          NULL,
          channel_props,
        },
        { NULL }
    };

    g_type_class_add_private (object_class,
                              sizeof(HazeContactListChannelPrivate));

    object_class->constructor = haze_contact_list_channel_constructor;

    object_class->dispose = haze_contact_list_channel_dispose;
    object_class->finalize = haze_contact_list_channel_finalize;

    object_class->get_property = haze_contact_list_channel_get_property;
    object_class->set_property = haze_contact_list_channel_set_property;


    param_spec = g_param_spec_object ("connection", "HazeConnection object",
        "Haze connection object that owns this contact list channel object.",
        HAZE_TYPE_CONNECTION,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property (object_class, PROP_CONNECTION, param_spec);

    param_spec = g_param_spec_boxed ("interfaces", "Extra D-Bus interfaces",
        "Additional Channel.Interface.* interfaces",
        G_TYPE_STRV,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property (object_class, PROP_INTERFACES, param_spec);

    param_spec = g_param_spec_string ("target-id", "Contact list name",
        "The stringy name of the contact list (\"subscribe\" etc.)",
        NULL,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property (object_class, PROP_TARGET_ID, param_spec);

    param_spec = g_param_spec_uint ("initiator-handle", "Initiator's handle",
        "The contact who initiated the channel",
        0, G_MAXUINT32, 0,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property (object_class, PROP_INITIATOR_HANDLE,
        param_spec);

    param_spec = g_param_spec_string ("initiator-id", "Initiator's ID",
        "The string obtained by inspecting the initiator-handle",
        NULL,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property (object_class, PROP_INITIATOR_ID,
        param_spec);

    param_spec = g_param_spec_boolean ("requested", "Requested?",
        "True if this channel was requested by the local user",
        FALSE,
        G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property (object_class, PROP_REQUESTED, param_spec);

    g_object_class_override_property (object_class, PROP_OBJECT_PATH,
            "object-path");
    g_object_class_override_property (object_class, PROP_CHANNEL_TYPE,
            "channel-type");
    g_object_class_override_property (object_class, PROP_HANDLE_TYPE,
            "handle-type");
    g_object_class_override_property (object_class, PROP_HANDLE, "handle");
    g_object_class_override_property (object_class, PROP_CHANNEL_DESTROYED,
        "channel-destroyed");
    g_object_class_override_property (object_class, PROP_CHANNEL_PROPERTIES,
        "channel-properties");


    tp_group_mixin_class_init (object_class,
        G_STRUCT_OFFSET (HazeContactListChannelClass, group_class),
        _haze_contact_list_channel_add_member_cb,
        _haze_contact_list_channel_remove_member_cb);

    if (!properties_mixin_initialized)
    {
        properties_mixin_initialized = TRUE;
        klass->properties_class.interfaces = prop_interfaces;
        tp_dbus_properties_mixin_class_init (object_class,
            G_STRUCT_OFFSET (HazeContactListChannelClass, properties_class));

        tp_group_mixin_init_dbus_properties (object_class);
    }
}

static void
haze_contact_list_channel_close (TpSvcChannel *iface,
                                 DBusGMethodInvocation *context)
{
    HazeContactListChannel *self = HAZE_CONTACT_LIST_CHANNEL (iface);
    HazeContactListChannelPrivate *priv;

    g_assert (HAZE_IS_CONTACT_LIST_CHANNEL (self));

    priv = self->priv;

    if (priv->handle_type == TP_HANDLE_TYPE_LIST)
    {
        GError e = { TP_ERRORS, TP_ERROR_NOT_IMPLEMENTED,
            "you may not close contact list channels" };

        dbus_g_method_return_error (context, &e);
    }
    else /* TP_HANDLE_TYPE_GROUP */
    {
        if (tp_handle_set_size (self->group.members) == 0)
        {
            priv->closed = TRUE;
            tp_svc_channel_emit_closed ((TpSvcChannel *)self);
            tp_svc_channel_return_from_close (context);
        }
        else
        {
            GError e = { TP_ERRORS, TP_ERROR_NOT_AVAILABLE,
                "This group is not empty, so you cannot close it." };
            dbus_g_method_return_error (context, &e);
        }
    }
}

static void
haze_contact_list_channel_get_channel_type (TpSvcChannel *iface,
                                            DBusGMethodInvocation *context)
{
    tp_svc_channel_return_from_get_channel_type (context,
        TP_IFACE_CHANNEL_TYPE_CONTACT_LIST);
}

static void
haze_contact_list_channel_get_handle (TpSvcChannel *iface,
                                     DBusGMethodInvocation *context)
{
    HazeContactListChannel *self = HAZE_CONTACT_LIST_CHANNEL (iface);
    HazeContactListChannelPrivate *priv;

    g_assert (HAZE_IS_CONTACT_LIST_CHANNEL (self));

    priv = self->priv;

    tp_svc_channel_return_from_get_handle (context, priv->handle_type,
        priv->handle);
}

static void
haze_contact_list_channel_get_interfaces (TpSvcChannel *self,
                                          DBusGMethodInvocation *context)
{
    tp_svc_channel_return_from_get_interfaces (context,
        haze_contact_list_channel_interfaces);
}

static void
channel_iface_init (gpointer g_iface, gpointer iface_data)
{
  TpSvcChannelClass *klass = (TpSvcChannelClass *)g_iface;

#define IMPLEMENT(x) tp_svc_channel_implement_##x (\
    klass, haze_contact_list_channel_##x)
  IMPLEMENT(close);
  IMPLEMENT(get_channel_type);
  IMPLEMENT(get_handle);
  IMPLEMENT(get_interfaces);
#undef IMPLEMENT
}
