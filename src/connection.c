/*
 * connection.c - HazeConnection source
 * Copyright (C) 2007 Will Thompson
 * Copyright (C) 2007-2009 Collabora Ltd.
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

#include <telepathy-glib/telepathy-glib.h>
#include <telepathy-glib/telepathy-glib-dbus.h>

#include <libpurple/accountopt.h>
#include <libpurple/version.h>

#include "debug.h"
#include "defines.h"
#include "connection-manager.h"
#include "connection.h"
#include "connection-presence.h"
#include "connection-aliasing.h"
#include "connection-avatars.h"
#include "connection-mail.h"
#include "extensions/extensions.h"
#include "request.h"

#include "connection-capabilities.h"

#ifdef HAVE_LIBINTL_H
#   include <libintl.h>
#else
#   define dgettext(domain, msgid) (msgid)
#endif

enum
{
    PROP_PARAMETERS = 1,
    PROP_USERNAME,
    PROP_PASSWORD,
    PROP_PRPL_ID,
    PROP_PRPL_INFO,

    LAST_PROPERTY
} HazeConnectionProperties;

G_DEFINE_TYPE_WITH_CODE(HazeConnection,
    haze_connection,
    TP_TYPE_BASE_CONNECTION,
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_DBUS_PROPERTIES,
        tp_dbus_properties_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_PRESENCE1,
        tp_presence_mixin_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_ALIASING1,
        haze_connection_aliasing_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_AVATARS1,
        haze_connection_avatars_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_CONTACT_CAPABILITIES1,
        haze_connection_contact_capabilities_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_CONTACT_LIST1,
        tp_base_contact_list_mixin_list_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_CONTACT_GROUPS1,
        tp_base_contact_list_mixin_groups_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_CONTACT_BLOCKING1,
        tp_base_contact_list_mixin_blocking_iface_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_SVC_CONNECTION_INTERFACE_MAIL_NOTIFICATION1,
        haze_connection_mail_iface_init);
    );

static const gchar * implemented_interfaces[] = {
    /* Conditionally present */

    TP_IFACE_CONNECTION_INTERFACE_AVATARS1,
    TP_IFACE_CONNECTION_INTERFACE_MAIL_NOTIFICATION1,
    TP_IFACE_CONNECTION_INTERFACE_CONTACT_BLOCKING1,
#   define HAZE_NUM_CONDITIONAL_INTERFACES 3

    /* Always present */

    TP_IFACE_CONNECTION_INTERFACE_CONTACT_LIST1,
    TP_IFACE_CONNECTION_INTERFACE_CONTACT_GROUPS1,
    TP_IFACE_CONNECTION_INTERFACE_PRESENCE1,
    TP_IFACE_CONNECTION_INTERFACE_CONTACT_CAPABILITIES1,
    /* TODO: This is a lie.  Not all protocols supported by libpurple
     *       actually have the concept of a user-settable alias, but
     *       there's no way for the UI to know (yet).
     */
    TP_IFACE_CONNECTION_INTERFACE_ALIASING1,
    NULL
};

static void
add_always_present_connection_interfaces (GPtrArray *interfaces)
{
  const gchar **iter;

  for (iter = implemented_interfaces + HAZE_NUM_CONDITIONAL_INTERFACES;
      *iter != NULL; iter++)
    g_ptr_array_add (interfaces, (gchar *) *iter);
}

static GPtrArray *
haze_connection_get_interfaces_always_present (TpBaseConnection *base)
{
  GPtrArray *interfaces;

  interfaces = TP_BASE_CONNECTION_CLASS (
      haze_connection_parent_class)->get_interfaces_always_present (base);

  add_always_present_connection_interfaces (interfaces);

  return interfaces;
}

static void add_optional_connection_interfaces (GPtrArray *ifaces,
        PurplePluginProtocolInfo *prpl_info);

/* Returns a (transfer container) not NULL terminated of (const gchar *)
 * interface names. */
GPtrArray *
haze_connection_dup_implemented_interfaces (PurplePluginProtocolInfo *prpl_info)
{
    GPtrArray *ifaces;

    ifaces = g_ptr_array_new ();
    add_always_present_connection_interfaces (ifaces);
    add_optional_connection_interfaces (ifaces, prpl_info);

    return ifaces;
}

struct _HazeConnectionPrivate
{
    gchar *username;
    gchar *password;
    GHashTable *parameters;

    gchar *prpl_id;
    PurplePluginProtocolInfo *prpl_info;

    /* Set if purple_account_request_password() was called */
    gpointer password_request;

    /* Set if purple_account_disconnect has been called or is scheduled to be
     * called, so should not be called again.
     */
    gboolean disconnecting;

    /* Set to TRUE when purple_account_connect has been called. */
    gboolean connect_called;

    gboolean dispose_has_run;
};

#define PC_GET_BASE_CONN(pc) \
    (ACCOUNT_GET_TP_BASE_CONNECTION (purple_connection_get_account (pc)))

static gboolean
protocol_info_supports_avatar (PurplePluginProtocolInfo *prpl_info)
{
    return (prpl_info->icon_spec.format != NULL);
}

static gboolean
protocol_info_supports_blocking (PurplePluginProtocolInfo *prpl_info)
{
    return (prpl_info->add_deny != NULL);
}

static gboolean
protocol_info_supports_mail_notification (PurplePluginProtocolInfo *prpl_info)
{
    return ((prpl_info->options & OPT_PROTO_MAIL_CHECK) != 0);
}

static void
add_optional_connection_interfaces (GPtrArray *ifaces,
        PurplePluginProtocolInfo *prpl_info)
{
    if (protocol_info_supports_avatar (prpl_info))
        g_ptr_array_add (ifaces,
                TP_IFACE_CONNECTION_INTERFACE_AVATARS1);

    if (protocol_info_supports_blocking (prpl_info))
        g_ptr_array_add (ifaces,
                TP_IFACE_CONNECTION_INTERFACE_CONTACT_BLOCKING1);

    if (protocol_info_supports_mail_notification (prpl_info))
        g_ptr_array_add (ifaces,
                TP_IFACE_CONNECTION_INTERFACE_MAIL_NOTIFICATION1);
}

static void
connected_cb (PurpleConnection *pc)
{
    TpBaseConnection *base_conn = PC_GET_BASE_CONN (pc);
    HazeConnection *conn = HAZE_CONNECTION (base_conn);
    PurplePluginProtocolInfo *prpl_info = HAZE_CONNECTION_GET_PRPL_INFO (conn);
    GPtrArray *ifaces;

    ifaces = g_ptr_array_new ();
    add_optional_connection_interfaces (ifaces, prpl_info);
    g_ptr_array_add (ifaces, NULL);

    tp_base_connection_add_interfaces (base_conn,
            (const gchar **) ifaces->pdata);
    g_ptr_array_unref (ifaces);

    tp_base_contact_list_set_list_received (
        (TpBaseContactList *) conn->contact_list);

    tp_base_connection_change_status (base_conn,
        TP_CONNECTION_STATUS_CONNECTED,
        TP_CONNECTION_STATUS_REASON_REQUESTED);
}

static void
map_purple_error_to_tp (
    PurpleConnectionError purple_reason,
    gboolean while_connecting,
    TpConnectionStatusReason *tp_reason,
    const gchar **tp_error_name)
{
  g_assert (tp_reason != NULL);
  g_assert (tp_error_name != NULL);

#define set_both(suffix) \
  G_STMT_START { \
    *tp_reason = TP_CONNECTION_STATUS_REASON_ ## suffix; \
    *tp_error_name = TP_ERROR_STR_ ## suffix; \
  } G_STMT_END

#define trivial_case(suffix) \
  case PURPLE_CONNECTION_ERROR_ ## suffix: \
    set_both (suffix); \
    break;

  switch (purple_reason)
    {
      case PURPLE_CONNECTION_ERROR_NETWORK_ERROR:
        if (while_connecting)
          *tp_error_name = TP_ERROR_STR_CONNECTION_FAILED;
        else
          *tp_error_name = TP_ERROR_STR_CONNECTION_LOST;

        *tp_reason = TP_CONNECTION_STATUS_REASON_NETWORK_ERROR;
        break;

      case PURPLE_CONNECTION_ERROR_AUTHENTICATION_FAILED:
      case PURPLE_CONNECTION_ERROR_INVALID_USERNAME:
      /* TODO: the following don't really match the tp reason but it's the
       * nearest match.  Invalid settings shouldn't get this far in the first
       * place, and we ought to have some code for having no authentication
       * mechanisms in common with the server. But the latter is currently a
       * moot point since libpurple doesn't use this anywhere besides Jabber.
       */
      case PURPLE_CONNECTION_ERROR_AUTHENTICATION_IMPOSSIBLE:
      case PURPLE_CONNECTION_ERROR_INVALID_SETTINGS:

      /* TODO: This is not a very useful error. But it's fatal in libpurple —
       * it's used for things like the ICQ server telling you that you're
       * temporarily banned—so should map to a fatal error in Telepathy.
       *
       * We ought really to have an error case for unrecoverable server errors
       * where the best we can do is present a human-readable error from the
       * server.
       */
      case PURPLE_CONNECTION_ERROR_OTHER_ERROR:
        set_both (AUTHENTICATION_FAILED);
        break;

      case PURPLE_CONNECTION_ERROR_NO_SSL_SUPPORT:
        *tp_reason = TP_CONNECTION_STATUS_REASON_ENCRYPTION_ERROR;
        *tp_error_name = TP_ERROR_STR_ENCRYPTION_NOT_AVAILABLE;
        break;

      case PURPLE_CONNECTION_ERROR_NAME_IN_USE:
        if (while_connecting)
          *tp_error_name = TP_ERROR_STR_ALREADY_CONNECTED;
        else
          *tp_error_name = TP_ERROR_STR_CONNECTION_REPLACED;

        *tp_reason = TP_CONNECTION_STATUS_REASON_NAME_IN_USE;
        break;

      case PURPLE_CONNECTION_ERROR_CERT_OTHER_ERROR:
        *tp_reason = TP_CONNECTION_STATUS_REASON_CERT_OTHER_ERROR;
        *tp_error_name = TP_ERROR_STR_CERT_INVALID;
        break;

      /* These members of the libpurple enum map 1-1 to the Telepathy enum and
       * to similarly-named D-Bus error names.
       */
      trivial_case (ENCRYPTION_ERROR)
      trivial_case (CERT_NOT_PROVIDED)
      trivial_case (CERT_UNTRUSTED)
      trivial_case (CERT_EXPIRED)
      trivial_case (CERT_NOT_ACTIVATED)
      trivial_case (CERT_HOSTNAME_MISMATCH)
      trivial_case (CERT_FINGERPRINT_MISMATCH)
      trivial_case (CERT_SELF_SIGNED)

      default:
        g_warning ("report_disconnect_cb: invalid PurpleDisconnectReason %u",
            purple_reason);
        *tp_reason = TP_CONNECTION_STATUS_REASON_NONE_SPECIFIED;
        *tp_error_name = TP_ERROR_STR_DISCONNECTED;
    }

#undef trivial_case
#undef set_both
}

static void
haze_report_disconnect_reason (PurpleConnection *gc,
                               PurpleConnectionError reason,
                               const char *text)
{
  PurpleAccount *account = purple_connection_get_account (gc);
  HazeConnection *conn = ACCOUNT_GET_HAZE_CONNECTION (account);
  HazeConnectionPrivate *priv = conn->priv;
  TpBaseConnection *base_conn = ACCOUNT_GET_TP_BASE_CONNECTION (account);
  GHashTable *details;
  TpConnectionStatusReason tp_reason;
  const gchar *tp_error_name;

  /* When a connection error is reported by libpurple, an idle callback to
   * purple_account_disconnect is added.
   */
  priv->disconnecting = TRUE;

  map_purple_error_to_tp (reason,
      (tp_base_connection_get_status (base_conn) ==
         TP_CONNECTION_STATUS_CONNECTING),
      &tp_reason, &tp_error_name);
  details = tp_asv_new ("debug-message", G_TYPE_STRING, text, NULL);
  tp_base_connection_disconnect_with_dbus_error (base_conn, tp_error_name,
      details, tp_reason);
  g_hash_table_unref (details);
}

static gboolean
idle_finish_shutdown (gpointer data)
{
  tp_base_connection_finish_shutdown (TP_BASE_CONNECTION (data));
  return FALSE;
}

static void
disconnected_cb (PurpleConnection *pc)
{
    PurpleAccount *account = purple_connection_get_account (pc);
    HazeConnection *conn = ACCOUNT_GET_HAZE_CONNECTION (account);
    HazeConnectionPrivate *priv = conn->priv;
    TpBaseConnection *base_conn = ACCOUNT_GET_TP_BASE_CONNECTION (account);

    priv->disconnecting = TRUE;

    if (tp_base_connection_get_status (base_conn) !=
        TP_CONNECTION_STATUS_DISCONNECTED)
    {
        /* Because we have report_disconnect_reason, if status is not already
         * DISCONNECTED, we know that it was requested. */
        tp_base_connection_change_status (base_conn,
            TP_CONNECTION_STATUS_DISCONNECTED,
            TP_CONNECTION_STATUS_REASON_REQUESTED);

    }

    /* Call tp_base_connection_finish_shutdown () in an idle because calling it
     * might lead to the HazeConnection being destroyed, which would mean the
     * PurpleAccount were destroyed, but we're currently inside libpurple code
     * that uses it.
     */
    g_idle_add (idle_finish_shutdown, conn);
}

static void
_warn_unhandled_parameter (const gchar *key,
                           const GValue *value,
                           gpointer user_data)
{
    g_warning ("received an unknown parameter '%s'; ignoring", key);
}

static void
set_option (
    PurpleAccount *account,
    const PurpleAccountOption *option,
    GHashTable *params)
{
  if (g_hash_table_lookup (params, option->pref_name) == NULL)
    return;

  switch (option->type)
    {
    case PURPLE_PREF_BOOLEAN:
      purple_account_set_bool (account, option->pref_name,
          tp_asv_get_boolean (params, option->pref_name, NULL));
      break;
    case PURPLE_PREF_INT:
      purple_account_set_int (account, option->pref_name,
          tp_asv_get_int32 (params, option->pref_name, NULL));
      break;
    case PURPLE_PREF_STRING:
    case PURPLE_PREF_STRING_LIST:
      purple_account_set_string (account, option->pref_name,
          tp_asv_get_string (params, option->pref_name));
      break;
    default:
      g_warning ("option '%s' has unhandled type %u",
          option->pref_name, option->type);
    }

  g_hash_table_remove (params, option->pref_name);
}

/**
 * haze_connection_create_account:
 *
 * Attempts to create a PurpleAccount corresponding to this connection. Must be
 * called immediately after constructing a connection. It's a shame GObject
 * constructors can't fail.
 *
 * Returns: %TRUE if the account was successfully created and hooked up;
 *          %FALSE with @error set in the TP_ERROR domain if the account
 *          already existed or another error occurred.
 */
gboolean
haze_connection_create_account (HazeConnection *self,
                                GError **error)
{
    HazeConnectionPrivate *priv = self->priv;
    GHashTable *params = priv->parameters;
    PurplePluginProtocolInfo *prpl_info = priv->prpl_info;
    GList *l;

    g_return_val_if_fail (self->account == NULL, FALSE);

    if (purple_accounts_find (priv->username, priv->prpl_id) != NULL)
      {
        g_set_error (error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
            "a connection already exists to %s on %s", priv->username,
            priv->prpl_id);
        return FALSE;
      }

    self->account = purple_account_new (priv->username, priv->prpl_id);
    purple_accounts_add (self->account);

    if (priv->password != NULL)
      purple_account_set_password (self->account, priv->password);

    self->account->ui_data = self;

    for (l = prpl_info->protocol_options; l != NULL; l = l->next)
      set_option (self->account, l->data, params);

    g_hash_table_foreach (params, (GHFunc) _warn_unhandled_parameter, "lala");

    return TRUE;
}

static void
_haze_connection_password_manager_prompt_cb (GObject *source,
    GAsyncResult *result,
    gpointer user_data)
{
  HazeConnection *self = user_data;
  HazeConnectionPrivate *priv = self->priv;
  TpBaseConnection *base_conn = (TpBaseConnection *) self;
  const GString *password;
  GError *error = NULL;

  password = tp_simple_password_manager_prompt_finish (
      TP_SIMPLE_PASSWORD_MANAGER (source), result, &error);

  if (error != NULL)
    {
      DEBUG ("Simple password manager failed: %s", error->message);

      if (priv->password_request)
        {
          haze_request_password_cb (priv->password_request, NULL);
          /* no need to call purple_account_disconnect(): the prpl will take
           * the account offline. If we're lucky it'll use an
           * AUTHENTICATION_FAILED-type message.
           */
        }
      else if (tp_base_connection_get_status (base_conn) !=
          TP_CONNECTION_STATUS_DISCONNECTED)
        {
          tp_base_connection_disconnect_with_dbus_error (base_conn,
              tp_error_get_dbus_name (error->code), NULL,
              TP_CONNECTION_STATUS_REASON_AUTHENTICATION_FAILED);
          /* no need to call purple_account_disconnect because _connect
           * was never called ...
           */
        }

      g_error_free (error);
      return;
    }

  g_free (priv->password);
  priv->password = g_strdup (password->str);

  if (priv->password_request)
    {
      haze_request_password_cb (priv->password_request, priv->password);
    }
  else
    {
      purple_account_set_password (self->account, priv->password);

      purple_account_set_enabled(self->account, UI_ID, TRUE);
      purple_account_connect (self->account);
      priv->connect_called = TRUE;
    }
}

static gboolean
_haze_connection_start_connecting (TpBaseConnection *base,
                                   GError **error)
{
    HazeConnection *self = HAZE_CONNECTION(base);
    HazeConnectionPrivate *priv = self->priv;
    TpHandleRepoIface *contact_handles =
        tp_base_connection_get_handles (base, TP_ENTITY_TYPE_CONTACT);
    const gchar *password;
    TpHandle self_handle;

    g_return_val_if_fail (self->account != NULL, FALSE);

    self_handle = tp_handle_ensure (contact_handles,
        purple_account_get_username (self->account), NULL, error);
    if (self_handle == 0)
      return FALSE;

    tp_base_connection_set_self_handle (base, self_handle);

    tp_base_connection_change_status(base, TP_CONNECTION_STATUS_CONNECTING,
                                     TP_CONNECTION_STATUS_REASON_REQUESTED);

    /* We systematically enable mail notification to avoid bugs in protocol
     * like GMail and MySpace where you need to do an action before connecting
     * to start receiving the notifications. */
    purple_account_set_check_mail(self->account, TRUE);

    /* check whether we need to pop up an auth channel */
    password = purple_account_get_password (self->account);

    if (password == NULL
        && !(priv->prpl_info->options & OPT_PROTO_NO_PASSWORD)
        && !(priv->prpl_info->options & OPT_PROTO_PASSWORD_OPTIONAL))
      {
        /* pop up auth channel */
        tp_simple_password_manager_prompt_async (self->password_manager,
            _haze_connection_password_manager_prompt_cb, self);
      }
    else
      {
        purple_account_set_enabled(self->account, UI_ID, TRUE);
        purple_account_connect (self->account);
        priv->connect_called = TRUE;
      }

    return TRUE;
}

void
haze_connection_request_password (PurpleAccount *account,
                                  void *user_data)
{
    HazeConnection *self = ACCOUNT_GET_HAZE_CONNECTION (account);
    HazeConnectionPrivate *priv = self->priv;

    priv->password_request = user_data;

    /* pop up auth channel */
    tp_simple_password_manager_prompt_async (self->password_manager,
                                             _haze_connection_password_manager_prompt_cb,
                                             self);
}

void
haze_connection_cancel_password_request (PurpleAccount *account)
{
    HazeConnection *self = ACCOUNT_GET_HAZE_CONNECTION (account);
    HazeConnectionPrivate *priv = self->priv;

    priv->password_request = NULL;
}

static void
_haze_connection_shut_down (TpBaseConnection *base)
{
    HazeConnection *self = HAZE_CONNECTION(base);
    HazeConnectionPrivate *priv = self->priv;

    if(!priv->disconnecting && priv->connect_called)
      {
        priv->disconnecting = TRUE;
        purple_account_disconnect(self->account);
      }
    else if (!priv->connect_called)
      {
        /* purple_account_connect was never actually called, so we
         * won't get to disconnected_cb and so finish_shutdown won't
         * be called unless we call it here. */
        tp_base_connection_finish_shutdown (base);
      }
}

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
        TpHandleRepoIface *repos[TP_NUM_ENTITY_TYPES])
{
    repos[TP_ENTITY_TYPE_CONTACT] =
        tp_dynamic_handle_repo_new (TP_ENTITY_TYPE_CONTACT, _contact_normalize,
                                    base);
    /* repos[TP_ENTITY_TYPE_ROOM] = XXX MUC */
}

static GPtrArray *
_haze_connection_create_channel_managers (TpBaseConnection *base)
{
    HazeConnection *self = HAZE_CONNECTION(base);
    GPtrArray *channel_managers = g_ptr_array_new ();

    self->im_factory = HAZE_IM_CHANNEL_FACTORY (
        g_object_new (HAZE_TYPE_IM_CHANNEL_FACTORY, "connection", self, NULL));
    g_ptr_array_add (channel_managers, self->im_factory);

    self->password_manager = tp_simple_password_manager_new (
        TP_BASE_CONNECTION (self));
    g_ptr_array_add (channel_managers, self->password_manager);

    return channel_managers;
}

static gchar *
haze_connection_get_unique_connection_name(TpBaseConnection *base)
{
    HazeConnection *self = HAZE_CONNECTION(base);

    return g_strdup (purple_account_get_username (self->account));
}

static void
haze_connection_get_property (GObject    *object,
                              guint       property_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
    HazeConnection *self = HAZE_CONNECTION (object);
    HazeConnectionPrivate *priv = self->priv;

    switch (property_id) {
        case PROP_PARAMETERS:
            g_value_set_boxed (value, priv->parameters);
            break;
        case PROP_USERNAME:
            g_value_set_string (value, priv->username);
            break;
        case PROP_PASSWORD:
            g_value_set_string (value, priv->password);
            break;
        case PROP_PRPL_ID:
            g_value_set_string (value, priv->prpl_id);
            break;
        case PROP_PRPL_INFO:
            g_value_set_pointer (value, priv->prpl_info);
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
    HazeConnectionPrivate *priv = self->priv;

    switch (property_id) {
        case PROP_USERNAME:
            priv->username = g_value_dup_string (value);
            break;
        case PROP_PASSWORD:
            priv->password = g_value_dup_string (value);
            break;
        case PROP_PARAMETERS:
            priv->parameters = g_value_dup_boxed (value);
            break;
        case PROP_PRPL_ID:
            g_free (priv->prpl_id);
            priv->prpl_id = g_value_dup_string (value);
            break;
        case PROP_PRPL_INFO:
            priv->prpl_info = g_value_get_pointer (value);
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
    GObject *object = (GObject *) self;
    HazeConnectionPrivate *priv = self->priv;

    DEBUG ("Post-construction: (HazeConnection *)%p", self);

    self->acceptable_avatar_mime_types = NULL;

    priv->dispose_has_run = FALSE;

    priv->disconnecting = FALSE;

    self->contact_list = HAZE_CONTACT_LIST (
        g_object_new (HAZE_TYPE_CONTACT_LIST, "connection", self, NULL));

    haze_connection_presence_init (object);
    haze_connection_mail_init (object);

    return (GObject *)self;
}

static void
haze_connection_dispose (GObject *object)
{
    HazeConnection *self = HAZE_CONNECTION(object);
    HazeConnectionPrivate *priv = self->priv;

    if (priv->dispose_has_run)
        return;

    priv->dispose_has_run = TRUE;

    DEBUG ("disposing of (HazeConnection *)%p", self);

    g_hash_table_unref (priv->parameters);
    priv->parameters = NULL;

    G_OBJECT_CLASS (haze_connection_parent_class)->dispose (object);
}

static void
haze_connection_finalize (GObject *object)
{
    HazeConnection *self = HAZE_CONNECTION (object);
    HazeConnectionPrivate *priv = self->priv;

    tp_presence_mixin_finalize (object);

    g_strfreev (self->acceptable_avatar_mime_types);
    g_free (priv->username);
    g_free (priv->password);

    if (self->account != NULL)
      {
        DEBUG ("deleting account %s", self->account->username);
        purple_accounts_delete (self->account);
      }

    G_OBJECT_CLASS (haze_connection_parent_class)->finalize (object);
}

static void
haze_connection_fill_contact_attributes (TpBaseConnection *base,
    const gchar *dbus_interface,
    TpHandle handle,
    TpContactAttributeMap *attributes)
{
  HazeConnection *self = HAZE_CONNECTION (base);

  if (haze_connection_aliasing_fill_contact_attributes (self,
        dbus_interface, handle, attributes))
    return;

  if (haze_connection_avatars_fill_contact_attributes (self,
        dbus_interface, handle, attributes))
    return;

  if (haze_connection_contact_capabilities_fill_contact_attributes (self,
        dbus_interface, handle, attributes))
    return;

  if (tp_base_contact_list_fill_contact_attributes (
        TP_BASE_CONTACT_LIST (self->contact_list),
        dbus_interface, handle, attributes))
    return;

  if (tp_presence_mixin_fill_contact_attributes ((GObject *) self,
        dbus_interface, handle, attributes))
    return;

  TP_BASE_CONNECTION_CLASS (haze_connection_parent_class)->
    fill_contact_attributes (base, dbus_interface, handle, attributes);
}

static void
haze_connection_class_init (HazeConnectionClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    TpBaseConnectionClass *base_class = TP_BASE_CONNECTION_CLASS (klass);
    GParamSpec *param_spec;
    static TpDBusPropertiesMixinPropImpl mail_props[] = {
        { "MailNotificationFlags", NULL, NULL },
        { "UnreadMailCount", NULL, NULL },
        { "UnreadMails", NULL, NULL },
        { "MailAddress", NULL, NULL },
        { NULL }
    };
    static TpDBusPropertiesMixinIfaceImpl prop_interfaces[] = {
        { TP_IFACE_CONNECTION_INTERFACE_ALIASING1,
            haze_connection_aliasing_properties_getter,
            NULL,
            NULL },     /* initialized a bit later */
        { TP_IFACE_CONNECTION_INTERFACE_AVATARS1,
            haze_connection_avatars_properties_getter,
            NULL,
            NULL },     /* initialized a bit later */
        { TP_IFACE_CONNECTION_INTERFACE_MAIL_NOTIFICATION1,
            haze_connection_mail_properties_getter,
            NULL,
            mail_props,
        },
        { NULL }
    };

    DEBUG ("Initializing (HazeConnectionClass *)%p", klass);

    g_type_class_add_private (klass, sizeof (HazeConnectionPrivate));
    object_class->get_property = haze_connection_get_property;
    object_class->set_property = haze_connection_set_property;
    object_class->constructor = haze_connection_constructor;
    object_class->dispose = haze_connection_dispose;
    object_class->finalize = haze_connection_finalize;

    base_class->create_handle_repos = _haze_connection_create_handle_repos;
    base_class->create_channel_managers =
        _haze_connection_create_channel_managers;
    base_class->get_unique_connection_name =
        haze_connection_get_unique_connection_name;
    base_class->start_connecting = _haze_connection_start_connecting;
    base_class->shut_down = _haze_connection_shut_down;
    base_class->get_interfaces_always_present =
      haze_connection_get_interfaces_always_present;
    base_class->fill_contact_attributes =
      haze_connection_fill_contact_attributes;

    param_spec = g_param_spec_boxed ("parameters", "gchar * => GValue",
        "Connection parameters (password, etc.)",
        G_TYPE_HASH_TABLE,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property (object_class, PROP_PARAMETERS, param_spec);

    param_spec = g_param_spec_string ("username", "username",
        "protocol plugin username", NULL,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property (object_class, PROP_USERNAME, param_spec);

    param_spec = g_param_spec_string ("password", "password",
        "protocol plugin password, or NULL if none", NULL,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property (object_class, PROP_PASSWORD, param_spec);

    param_spec = g_param_spec_string ("prpl-id", "protocol plugin ID",
        "protocol plugin ID", NULL,
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property (object_class, PROP_PRPL_ID, param_spec);

    param_spec = g_param_spec_pointer ("prpl-info", "PurplePluginProtocolInfo",
        "protocol plugin info",
        G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    g_object_class_install_property (object_class, PROP_PRPL_INFO, param_spec);

    prop_interfaces[0].props = haze_connection_aliasing_properties;
    prop_interfaces[1].props = haze_connection_avatars_properties;
    klass->properties_class.interfaces = prop_interfaces;
    tp_dbus_properties_mixin_class_init (object_class,
        G_STRUCT_OFFSET (HazeConnectionClass, properties_class));

    tp_base_contact_list_mixin_class_init (base_class);

    haze_connection_presence_class_init (object_class);
    haze_connection_aliasing_class_init (object_class);
    haze_connection_avatars_class_init (object_class);
}

static void
haze_connection_init (HazeConnection *self)
{
    DEBUG ("Initializing (HazeConnection *)%p", self);
    self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, HAZE_TYPE_CONNECTION,
                                              HazeConnectionPrivate);
}

static PurpleAccountUiOps
account_ui_ops =
{
    NULL,                                            /* notify_added */
    haze_connection_presence_account_status_changed, /* status_changed */
    NULL,                                            /* request_add */
    haze_request_authorize,                          /* request_authorize */
    haze_close_account_request,                      /* close_account_request */

    NULL, /* purple_reserved1 */
    NULL, /* purple_reserved2 */
    NULL, /* purple_reserved3 */
    NULL  /* purple_reserved4 */
};

PurpleAccountUiOps *
haze_get_account_ui_ops ()
{
    return &account_ui_ops;
}

static PurpleConnectionUiOps
connection_ui_ops =
{
    NULL,            /* connect_progress */
    connected_cb,    /* connected */
    disconnected_cb, /* disconnected */
    NULL,            /* notice */
    NULL,            /* report_disconnect */
    NULL,            /* network_connected */
    NULL,            /* network_disconnected */
    haze_report_disconnect_reason, /* report_disconnect_reason */

    NULL, /* _purple_reserved1 */
    NULL, /* _purple_reserved2 */
    NULL  /* _purple_reserved3 */
};

PurpleConnectionUiOps *
haze_get_connection_ui_ops ()
{
    return &connection_ui_ops;
}

const gchar *
haze_connection_handle_inspect (HazeConnection *conn,
                                TpEntityType handle_type,
                                TpHandle handle)
{
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
    TpHandleRepoIface *handle_repo =
        tp_base_connection_get_handles (base_conn, handle_type);
    g_assert (tp_handle_is_valid (handle_repo, handle, NULL));
    return tp_handle_inspect (handle_repo, handle);
}

/**
 * Get the group that "most" libpurple prpls will use for ungrouped contacts.
 */
const gchar *
haze_get_fallback_group (void)
{
  return dgettext ("pidgin", "Buddies");
}
