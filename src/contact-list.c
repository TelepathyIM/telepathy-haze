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

#include <telepathy-glib/dbus.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/intset.h>

#include "connection.h"
#include "contact-list.h"
#include "debug.h"

typedef struct _PublishRequestData PublishRequestData;

/** PublishRequestData:
 *
 *  Keeps track of the relevant callbacks to approve or deny a contact's publish
 *  request.
 */
struct _PublishRequestData {
    HazeContactList *self;
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
    g_free (prd->message);
    g_slice_free (PublishRequestData, prd);
}

struct _HazeContactListPrivate {
    HazeConnection *conn;

    /* Maps TpHandle to PublishRequestData, corresponding to the handles on
     * publish's local_pending.
     */
    GHashTable *pending_publish_requests;

    /* Contacts whose publish requests we've accepted or declined. */
    TpHandleSet *publishing_to;
    TpHandleSet *not_publishing_to;

    gboolean dispose_has_run;
};

static void haze_contact_list_mutable_init (TpMutableContactListInterface *);
static void haze_contact_list_groups_init (TpContactGroupListInterface *);
static void haze_contact_list_mutable_groups_init (
    TpMutableContactGroupListInterface *);
static void haze_contact_list_blockable_init (TpBlockableContactListInterface *);

G_DEFINE_TYPE_WITH_CODE(HazeContactList,
    haze_contact_list,
    TP_TYPE_BASE_CONTACT_LIST,
    G_IMPLEMENT_INTERFACE (TP_TYPE_MUTABLE_CONTACT_LIST,
      haze_contact_list_mutable_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_CONTACT_GROUP_LIST,
      haze_contact_list_groups_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_MUTABLE_CONTACT_GROUP_LIST,
      haze_contact_list_mutable_groups_init);
    G_IMPLEMENT_INTERFACE (TP_TYPE_BLOCKABLE_CONTACT_LIST,
      haze_contact_list_blockable_init))

static void
haze_contact_list_init (HazeContactList *self)
{
    HazeContactListPrivate *priv =
        (G_TYPE_INSTANCE_GET_PRIVATE((self), HAZE_TYPE_CONTACT_LIST,
                                     HazeContactListPrivate));

    self->priv = priv;

    priv->dispose_has_run = FALSE;
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

    self->priv->conn = HAZE_CONNECTION (tp_base_contact_list_get_connection (
        (TpBaseContactList *) self, NULL));
    g_assert (self->priv->conn != NULL);
    /* not reffed, for the moment */

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

static void buddy_added_cb (PurpleBuddy *buddy, gpointer unused);
static void buddy_removed_cb (PurpleBuddy *buddy, gpointer unused);

static TpHandleSet *
haze_contact_list_dup_contacts (TpBaseContactList *cl)
{
  HazeContactList *self = HAZE_CONTACT_LIST (cl);
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (self->priv->conn);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);
  /* The list initially contains anyone we're definitely publishing to.
   * Because libpurple, that's only people whose request we accepted during
   * this session :-( */
  TpHandleSet *handles = tp_handle_set_copy (self->priv->publishing_to);
  GSList *buddies = purple_find_buddies (self->priv->conn->account, NULL);
  GSList *sl_iter;
  GHashTableIter hash_iter;
  gpointer k;

  /* Also include anyone on our buddy list */
  for (sl_iter = buddies; sl_iter != NULL; sl_iter = sl_iter->next)
    {
      TpHandle handle = tp_handle_ensure (contact_repo,
          purple_buddy_get_name (sl_iter->data), NULL, NULL);

      if (G_LIKELY (handle != 0))
        tp_handle_set_add (handles, handle);
    }

  g_slist_free (buddies);

  /* Also include anyone with an outstanding request */
  g_hash_table_iter_init (&hash_iter, self->priv->pending_publish_requests);

  while (g_hash_table_iter_next (&hash_iter, &k, NULL))
    {
      tp_handle_set_add (handles, GPOINTER_TO_UINT (k));
    }

  return handles;
}

static void
haze_contact_list_dup_states (TpBaseContactList *cl,
    TpHandle contact,
    TpSubscriptionState *subscribe_out,
    TpSubscriptionState *publish_out,
    gchar **publish_request_out)
{
  HazeContactList *self = HAZE_CONTACT_LIST (cl);
  const gchar *bname = haze_connection_handle_inspect (self->priv->conn,
      TP_HANDLE_TYPE_CONTACT, contact);
  PurpleBuddy *buddy = purple_find_buddy (self->priv->conn->account, bname);
  TpSubscriptionState pub, sub;
  PublishRequestData *pub_req = g_hash_table_lookup (
      self->priv->pending_publish_requests, GUINT_TO_POINTER (contact));

  if (publish_request_out != NULL)
    *publish_request_out = NULL;

  if (buddy != NULL)
    {
      /* Well, it's on the contact list. Are we subscribed to its presence?
       * Who knows? Let's assume we are. */
      sub = TP_SUBSCRIPTION_STATE_YES;
    }
  else
    {
      /* We're definitely not subscribed. */
      sub = TP_SUBSCRIPTION_STATE_NO;
    }

  if (pub_req != NULL)
    {
      pub = TP_SUBSCRIPTION_STATE_ASK;

      if (publish_request_out != NULL)
        *publish_request_out = g_strdup (pub_req->message);
    }
  else if (tp_handle_set_is_member (self->priv->publishing_to, contact))
    {
      pub = TP_SUBSCRIPTION_STATE_YES;
    }
  else if (tp_handle_set_is_member (self->priv->not_publishing_to, contact))
    {
      pub = TP_SUBSCRIPTION_STATE_NO;
    }
  else
    {
      pub = TP_SUBSCRIPTION_STATE_UNKNOWN;
    }

  if (subscribe_out != NULL)
    *subscribe_out = sub;

  if (publish_out != NULL)
    *publish_out = pub;
}

static void
haze_contact_list_class_init (HazeContactListClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    TpBaseContactListClass *parent_class = TP_BASE_CONTACT_LIST_CLASS (klass);

    object_class->constructor = haze_contact_list_constructor;

    object_class->dispose = haze_contact_list_dispose;
    object_class->finalize = haze_contact_list_finalize;

    parent_class->dup_contacts = haze_contact_list_dup_contacts;
    parent_class->dup_states = haze_contact_list_dup_states;
    /* we assume the contact list does persist, which is the default */

    g_type_class_add_private (object_class,
                              sizeof(HazeContactListPrivate));

    purple_signal_connect (purple_blist_get_handle(), "buddy-added",
                           klass, PURPLE_CALLBACK(buddy_added_cb), NULL);
    purple_signal_connect (purple_blist_get_handle(), "buddy-removed",
                           klass, PURPLE_CALLBACK(buddy_removed_cb), NULL);
}

static void
buddy_added_cb (PurpleBuddy *buddy, gpointer unused)
{
    HazeConnection *conn = ACCOUNT_GET_HAZE_CONNECTION (buddy->account);
    HazeContactList *contact_list = conn->contact_list;
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
    TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (
        base_conn, TP_HANDLE_TYPE_CONTACT);
    const gchar *name = purple_buddy_get_name (buddy);
    TpHandle handle = tp_handle_ensure (contact_repo, name, NULL, NULL);
    const char *group_name;

    tp_base_contact_list_one_contact_changed (
        (TpBaseContactList *) contact_list, handle);

    group_name = purple_group_get_name (purple_buddy_get_group (buddy));
    tp_base_contact_list_one_contact_groups_changed (
        (TpBaseContactList *) contact_list, handle, &group_name, 1, NULL, 0);
}

static void
buddy_removed_cb (PurpleBuddy *buddy, gpointer unused)
{
    HazeConnection *conn = ACCOUNT_GET_HAZE_CONNECTION (buddy->account);
    TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
    HazeContactList *contact_list;
    TpHandleRepoIface *contact_repo;
    TpHandle handle;
    const char *group_name, *buddy_name;
    GSList *buddies, *l;
    gboolean last_instance = TRUE;

    /* Every buddy gets removed after disconnection, because the PurpleAccount
     * gets deleted.  So let's ignore removals when we're offline.
     */
    if (base_conn->status == TP_CONNECTION_STATUS_DISCONNECTED)
        return;

    contact_list = conn->contact_list;
    contact_repo = tp_base_connection_get_handles (base_conn,
        TP_HANDLE_TYPE_CONTACT);

    buddy_name = purple_buddy_get_name (buddy);
    handle = tp_handle_ensure (contact_repo, buddy_name, NULL, NULL);
    group_name = purple_group_get_name (purple_buddy_get_group (buddy));

    tp_base_contact_list_one_contact_groups_changed (
        (TpBaseContactList *) contact_list, handle, NULL, 0, &group_name, 1);

    buddies = purple_find_buddies (conn->account, buddy_name);

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
        tp_base_contact_list_one_contact_removed (
            (TpBaseContactList *) contact_list, handle);
    }
}


/* Objects needed or populated while iterating across the purple buddy list at
 * login.
 */
typedef struct _HandleContext {
    TpHandleRepoIface *contact_repo;
    TpHandleSet *add_handles;

    /* Map from group names (const char *) to (TpIntset *)s of handles */
    GHashTable *group_handles;
} HandleContext;

/* Removes the PublishRequestData for the given handle, from the
 * pending_publish_requests table, dropping its reference to that handle.
 */
static void
remove_pending_publish_request (HazeContactList *self,
                                TpHandle handle)
{
    gpointer h = GUINT_TO_POINTER (handle);
    gboolean removed;

    removed = g_hash_table_remove (self->priv->pending_publish_requests, h);
    g_assert (removed);
}

void
haze_contact_list_accept_publish_request (HazeContactList *self,
    TpHandle handle)
{
  gpointer key = GUINT_TO_POINTER (handle);
  PublishRequestData *request_data = g_hash_table_lookup (
      self->priv->pending_publish_requests, key);
  const gchar *bname = haze_connection_handle_inspect (self->priv->conn,
      TP_HANDLE_TYPE_CONTACT, handle);

  if (request_data == NULL)
    return;

  DEBUG ("allowing publish request for %s", bname);
  request_data->allow(request_data->data);

  tp_handle_set_add (self->priv->publishing_to, handle);
  remove_pending_publish_request (self, handle);

  tp_base_contact_list_one_contact_changed ((TpBaseContactList *) self,
      handle);
}

static void
haze_contact_list_authorize_publication_async (TpBaseContactList *cl,
    TpHandleSet *contacts,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  HazeContactList *self = HAZE_CONTACT_LIST (cl);
  TpIntsetFastIter iter;
  TpHandle handle;

  tp_intset_fast_iter_init (&iter, tp_handle_set_peek (contacts));

  while (tp_intset_fast_iter_next (&iter, &handle))
    haze_contact_list_accept_publish_request (self, handle);

  tp_simple_async_report_success_in_idle ((GObject *) self, callback,
      user_data, haze_contact_list_authorize_publication_async);
}

void
haze_contact_list_reject_publish_request (HazeContactList *self,
    TpHandle handle)
{
  gpointer key = GUINT_TO_POINTER (handle);
  PublishRequestData *request_data = g_hash_table_lookup (
      self->priv->pending_publish_requests, key);
  const gchar *bname = haze_connection_handle_inspect (self->priv->conn,
      TP_HANDLE_TYPE_CONTACT, handle);

  if (request_data == NULL)
    return;

  DEBUG ("denying publish request for %s", bname);
  request_data->deny(request_data->data);

  tp_handle_set_add (self->priv->not_publishing_to, handle);
  remove_pending_publish_request (self, handle);

  tp_base_contact_list_one_contact_changed ((TpBaseContactList *) self,
      handle);
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
    TpHandle remote_handle;
    PublishRequestData *request_data = publish_request_data_new ();

    /* This handle is owned by request_data, and is unreffed in
     * remove_pending_publish_request.
     */
    remote_handle = tp_handle_ensure (contact_repo, remote_user, NULL, NULL);
    request_data->self = g_object_ref (conn->contact_list);
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

    tp_base_contact_list_one_contact_changed ((TpBaseContactList *) self,
        remote_handle);

    return request_data;
}


void
haze_close_account_request (gpointer request_data_)
{
    PublishRequestData *request_data = request_data_;
    /* When 'request_data' is removed from the pending request table, its
     * reference to 'self' is dropped.  So, we take our own reference here,
     * in case the reference in 'request_data' was the last one. */
    HazeContactList *self = g_object_ref (request_data->self);
    TpHandle handle = request_data->handle;

    /* Note that adding the handle to @not_publishing_to has the side-effect
     * of ensuring that it remains valid long enough for us to signal the
     * change. */
    DEBUG ("cancelling publish request for handle %u", handle);
    tp_handle_set_add (self->priv->not_publishing_to, handle);
    remove_pending_publish_request (self, handle);

    tp_base_contact_list_one_contact_changed ((TpBaseContactList *) self,
        handle);

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

static void
haze_contact_list_request_subscription_async (TpBaseContactList *cl,
    TpHandleSet *contacts,
    const gchar *message,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  HazeContactList *self = HAZE_CONTACT_LIST (cl);
  TpIntsetFastIter iter;
  TpHandle handle;

  tp_intset_fast_iter_init (&iter, tp_handle_set_peek (contacts));

  while (tp_intset_fast_iter_next (&iter, &handle))
    haze_contact_list_request_subscription (self, handle, message);

  tp_simple_async_report_success_in_idle ((GObject *) self, callback,
      user_data, haze_contact_list_request_subscription_async);
}

static void
haze_contact_list_store_contacts_async (TpBaseContactList *cl,
    TpHandleSet *contacts,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  haze_contact_list_request_subscription_async (cl, contacts, "", callback,
      user_data);
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

static void
haze_contact_list_remove_contacts_async (TpBaseContactList *cl,
    TpHandleSet *contacts,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  HazeContactList *self = HAZE_CONTACT_LIST (cl);
  TpIntsetFastIter iter;
  TpHandle handle;

  tp_intset_fast_iter_init (&iter, tp_handle_set_peek (contacts));

  while (tp_intset_fast_iter_next (&iter, &handle))
    haze_contact_list_remove_contact (self, handle);

  tp_simple_async_report_success_in_idle ((GObject *) self, callback,
      user_data, haze_contact_list_remove_contacts_async);
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
    /* This actually has "ensure" semantics, and doesn't return a ref */
    PurpleGroup *group = purple_group_new (group_name);

    /* We have to reassure the TpBaseContactList that the group exists,
     * because libpurple doesn't have a group-added signal */
    tp_base_contact_list_groups_created ((TpBaseContactList *) self,
        &group_name, 1);

    g_return_if_fail (group != NULL);

    /* if the contact is in the group, we have nothing to do */
    if (purple_find_buddy_in_group (conn->account, bname, group) != NULL)
      return;

    buddy = purple_buddy_new (conn->account, bname, NULL);

    /* FIXME: This causes it to be added to 'subscribed' too. */
    purple_blist_add_buddy (buddy, NULL, group, NULL);
    purple_account_add_buddy (conn->account, buddy);
}

/* Prepare to @contacts from @group_name. If some of the @contacts are not in
 * any other group, add them to the fallback group, or if @group_name *is* the
 * fallback group, fail without doing anything else. */
static gboolean
haze_contact_list_prep_remove_from_group (HazeContactList *self,
    const gchar *group_name,
    TpHandleSet *contacts,
    GError **error)
{
  HazeConnection *conn = self->priv->conn;
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);
  PurpleAccount *account = conn->account;
  PurpleGroup *group = purple_find_group (group_name);
  TpIntsetFastIter iter;
  TpHandle handle;
  TpHandleSet *orphans;

  /* no such group? that was easy, we "already removed them" */
  if (group == NULL)
    return TRUE;

  orphans = tp_handle_set_new (contact_repo);

  tp_intset_fast_iter_init (&iter, tp_handle_set_peek (contacts));

  while (tp_intset_fast_iter_next (&iter, &handle))
    {
      gboolean is_in = FALSE;
      gboolean orphaned = TRUE;
      GSList *buddies;
      GSList *l;
      const gchar *bname =
          haze_connection_handle_inspect (conn, TP_HANDLE_TYPE_CONTACT,
              handle);

      g_assert (bname != NULL);

      buddies = purple_find_buddies (account, bname);

      for (l = buddies; l != NULL; l = l->next)
        {
          PurpleGroup *their_group = purple_buddy_get_group (l->data);

          if (their_group == group)
            is_in = TRUE;
          else
            orphaned = FALSE;
        }

      if (is_in && orphaned)
        tp_handle_set_add (orphans, handle);

      g_slist_free (buddies);
    }

  /* If they're in the group and it's their last group, we need to move
   * them to the fallback group. If the group they're being removed from *is*
   * the fallback group, we just fail (before we've actually done anything). */
  if (!tp_handle_set_is_empty (orphans))
    {
      const gchar *def_name = haze_get_fallback_group ();
      PurpleGroup *default_group = purple_group_new (def_name);

      /* We might have just created that group; libpurple doesn't have
       * a group-added signal, so tell TpBaseContactList about it */
      tp_base_contact_list_groups_created ((TpBaseContactList *) self,
          &def_name, 1);

      if (default_group == group)
        {
          g_set_error (error, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
              "Contacts can't be removed from '%s' unless they are in "
              "another group", group->name);
          return FALSE;
        }

      tp_intset_fast_iter_init (&iter, tp_handle_set_peek (orphans));

      while (tp_intset_fast_iter_next (&iter, &handle))
        {
          const gchar *bname =
              haze_connection_handle_inspect (conn, TP_HANDLE_TYPE_CONTACT,
                  handle);

          PurpleBuddy *copy = purple_buddy_new (conn->account, bname, NULL);
          purple_blist_add_buddy (copy, NULL, default_group, NULL);
          purple_account_add_buddy (account, copy);
        }
    }

  return TRUE;
}

/* haze_contact_list_prep_remove_from_group() must succeed first. */
static void
haze_contact_list_remove_many_from_group (HazeContactList *self,
    const gchar *group_name,
    TpHandleSet *contacts)
{
  PurpleAccount *account = self->priv->conn->account;
  PurpleGroup *group = purple_find_group (group_name);
  TpIntsetFastIter iter;
  TpHandle handle;

  if (group == NULL)
    return;

  tp_intset_fast_iter_init (&iter, tp_handle_set_peek (contacts));

  while (tp_intset_fast_iter_next (&iter, &handle))
    {
      GSList *buddies;
      GSList *l;
      const gchar *bname = haze_connection_handle_inspect (self->priv->conn,
          TP_HANDLE_TYPE_CONTACT, handle);

      buddies = purple_find_buddies (account, bname);

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
    }
}

gboolean
haze_contact_list_remove_from_group (HazeContactList *self,
    const gchar *group_name,
    TpHandle handle,
    GError **error)
{
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (self->priv->conn);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);
  gboolean ok;
  TpHandleSet *contacts = tp_handle_set_new_containing (contact_repo, handle);

  ok = haze_contact_list_prep_remove_from_group (self, group_name, contacts,
      error);

  if (ok)
    haze_contact_list_remove_many_from_group (self, group_name, contacts);

  tp_handle_set_destroy (contacts);
  return ok;
}

static void
haze_contact_list_mutable_init (TpMutableContactListInterface *vtable)
{
  /* we use the default _finish functions, which assume a GSimpleAsyncResult */
  vtable->request_subscription_async =
    haze_contact_list_request_subscription_async;
  vtable->authorize_publication_async =
    haze_contact_list_authorize_publication_async;
  vtable->remove_contacts_async = haze_contact_list_remove_contacts_async;
  /* this is about the best we can do for unsubscribe/unpublish */
  vtable->unsubscribe_async = haze_contact_list_remove_contacts_async;
  vtable->unpublish_async = haze_contact_list_remove_contacts_async;
  vtable->store_contacts_async = haze_contact_list_store_contacts_async;
  /* assume defaults: can change the contact list, and requests use the
   * message */
}

static GStrv
haze_contact_list_dup_groups (TpBaseContactList *cl G_GNUC_UNUSED)
{
  PurpleBlistNode *node;
  /* borrowed group name => NULL */
  GHashTable *groups = g_hash_table_new (g_str_hash, g_str_equal);
  GHashTableIter iter;
  gpointer k;
  GPtrArray *arr;

  for (node = purple_blist_get_root ();
      node != NULL;
      node = purple_blist_node_next (node, TRUE))
    {
      if (purple_blist_node_get_type (node) == PURPLE_BLIST_GROUP_NODE)
        g_hash_table_insert (groups, PURPLE_GROUP (node)->name, NULL);
    }

  arr = g_ptr_array_sized_new (g_hash_table_size (groups) + 1);

  g_hash_table_iter_init (&iter, groups);

  while (g_hash_table_iter_next (&iter, &k, NULL))
    g_ptr_array_add (arr, g_strdup (k));

  g_hash_table_unref (groups);
  g_ptr_array_add (arr, NULL);
  return (GStrv) g_ptr_array_free (arr, FALSE);
}

static GStrv
haze_contact_list_dup_contact_groups (TpBaseContactList *cl,
    TpHandle contact)
{
  HazeContactList *self = HAZE_CONTACT_LIST (cl);
  const gchar *bname = haze_connection_handle_inspect (self->priv->conn,
      TP_HANDLE_TYPE_CONTACT, contact);
  GSList *buddies, *sl_iter;
  GPtrArray *arr;

  g_return_val_if_fail (bname != NULL, NULL);

  buddies = purple_find_buddies (self->priv->conn->account, bname);

  arr = g_ptr_array_sized_new (g_slist_length (buddies));

  for (sl_iter = buddies; sl_iter != NULL; sl_iter = sl_iter->next)
    {
      PurpleGroup *group = purple_buddy_get_group (sl_iter->data);

      g_ptr_array_add (arr, g_strdup (purple_group_get_name (group)));
    }

  g_slist_free (buddies);
  g_ptr_array_add (arr, NULL);
  return (GStrv) g_ptr_array_free (arr, FALSE);
}

static TpHandleSet *
haze_contact_list_dup_group_members (TpBaseContactList *cl,
    const gchar *group_name)
{
  HazeContactList *self = HAZE_CONTACT_LIST (cl);
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (self->priv->conn);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);
  PurpleGroup *group = purple_find_group (group_name);
  PurpleBlistNode *contact, *buddy;
  TpHandleSet *members = tp_handle_set_new (contact_repo);

  if (group == NULL)
    return members;

  for (contact = purple_blist_node_get_first_child ((PurpleBlistNode *) group);
      contact != NULL;
      contact = purple_blist_node_get_sibling_next (contact))
    {
      if (G_UNLIKELY (purple_blist_node_get_type (contact) !=
            PURPLE_BLIST_CONTACT_NODE))
        {
          g_warning ("a child of a Group had unexpected type %d",
              purple_blist_node_get_type (contact));
          continue;
        }

      for (buddy = purple_blist_node_get_first_child (contact);
          buddy != NULL;
          buddy = purple_blist_node_get_sibling_next (buddy))
        {
          const gchar *bname;
          TpHandle handle;

          if (G_UNLIKELY (purple_blist_node_get_type (buddy) !=
                PURPLE_BLIST_BUDDY_NODE))
            {
              g_warning ("a child of a Contact had unexpected type %d",
                  purple_blist_node_get_type (buddy));
              continue;
            }

          bname = purple_buddy_get_name (PURPLE_BUDDY (buddy));
          handle = tp_handle_ensure (contact_repo, bname, NULL, NULL);

          if (G_LIKELY (handle != 0))
            tp_handle_set_add (members, handle);
        }
    }

  return members;
}

static gchar *
haze_contact_list_normalize_group (TpBaseContactList *cl G_GNUC_UNUSED,
    const gchar *s)
{
  /* By inspection of blist.c: group names are normalized by stripping
   * unprintable characters, then considering groups that collate to the
   * same thing in the current locale to be the same group. Empty group names
   * aren't allowed. */
  gchar *ret = purple_utf8_strip_unprintables (s);
  PurpleGroup *group;
  const gchar *group_name;

  if (tp_str_empty (ret))
    {
      g_free (ret);
      return NULL;
    }

  group = purple_find_group (ret);

  if (group != NULL)
    {
      group_name = purple_group_get_name (group);

      if (tp_strdiff (group_name, ret))
        {
          g_free (ret);
          return g_strdup (group_name);
        }
    }

  return ret;
}

static void
haze_contact_list_groups_init (TpContactGroupListInterface *vtable)
{
  vtable->dup_groups = haze_contact_list_dup_groups;
  vtable->dup_group_members = haze_contact_list_dup_group_members;
  vtable->dup_contact_groups = haze_contact_list_dup_contact_groups;
  vtable->normalize_group = haze_contact_list_normalize_group;
  /* assume default: groups aren't disjoint */
}

static void
haze_contact_list_set_contact_groups_async (TpBaseContactList *cl,
    TpHandle contact,
    const gchar * const *names,
    gsize n_names,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  HazeContactList *self = HAZE_CONTACT_LIST (cl);
  PurpleAccount *account = self->priv->conn->account;
  const gchar *fallback_group;
  const gchar *bname = haze_connection_handle_inspect (self->priv->conn,
      TP_HANDLE_TYPE_CONTACT, contact);
  gsize i;
  GSList *buddies, *l;

  g_assert (bname != NULL);

  if (n_names == 0)
    {
      /* the contact must be in some group */
      fallback_group = haze_get_fallback_group ();
      names = &fallback_group;
      n_names = 1;
    }

  /* put them in any groups they ought to be in */
  for (i = 0; i < n_names; i++)
    haze_contact_list_add_to_group (self, names[i], contact);

  /* remove them from any groups they ought to not be in */
  buddies = purple_find_buddies (account, bname);

  for (l = buddies; l != NULL; l = l->next)
    {
      PurpleGroup *group = purple_buddy_get_group (l->data);
      const gchar *group_name = purple_group_get_name (group);
      gboolean desired = FALSE;

      for (i = 0; i < n_names; i++)
        {
          if (!tp_strdiff (group_name, names[i]))
            {
              desired = TRUE;
              break;
            }
        }

      if (!desired)
        {
          purple_account_remove_buddy (account, l->data, group);
          purple_blist_remove_buddy (l->data);
        }
    }

  tp_simple_async_report_success_in_idle ((GObject *) self, callback,
      user_data, haze_contact_list_set_contact_groups_async);
}

static void
haze_contact_list_add_to_group_async (TpBaseContactList *cl,
    const gchar *group_name,
    TpHandleSet *contacts,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  HazeContactList *self = HAZE_CONTACT_LIST (cl);
  TpIntsetFastIter iter;
  TpHandle handle;
  /* This actually has "ensure" semantics, and doesn't return a ref */
  PurpleGroup *group = purple_group_new (group_name);

  /* We have to reassure the TpBaseContactList that the group exists,
   * because libpurple doesn't have a group-added signal */
  g_assert (group != NULL);
  tp_base_contact_list_groups_created ((TpBaseContactList *) self,
      &group_name, 1);

  tp_intset_fast_iter_init (&iter, tp_handle_set_peek (contacts));

  while (tp_intset_fast_iter_next (&iter, &handle))
    haze_contact_list_add_to_group (self, group_name, handle);

  tp_simple_async_report_success_in_idle ((GObject *) self, callback,
      user_data, haze_contact_list_add_to_group_async);
}

static void
haze_contact_list_remove_from_group_async (TpBaseContactList *cl,
    const gchar *group_name,
    TpHandleSet *contacts,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  HazeContactList *self = HAZE_CONTACT_LIST (cl);
  GError *error = NULL;

  if (haze_contact_list_prep_remove_from_group (self, group_name, contacts,
        &error))
    {
      haze_contact_list_remove_many_from_group (self, group_name, contacts);
      tp_simple_async_report_success_in_idle ((GObject *) cl, callback,
          user_data, haze_contact_list_remove_from_group_async);
    }
  else
    {
      g_simple_async_report_gerror_in_idle ((GObject *) cl, callback,
          user_data, error);
      g_clear_error (&error);
    }
}

static void
haze_contact_list_remove_group_async (TpBaseContactList *cl,
    const gchar *group_name,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  TpHandleSet *members = haze_contact_list_dup_group_members (cl, group_name);
  GError *error = NULL;

  if (haze_contact_list_prep_remove_from_group (HAZE_CONTACT_LIST (cl),
        group_name, members, &error))
    {
      PurpleGroup *group = purple_find_group (group_name);

      if (group != NULL)
        purple_blist_remove_group (group);

      tp_base_contact_list_groups_removed (cl, &group_name, 1);

      tp_simple_async_report_success_in_idle ((GObject *) cl, callback,
          user_data, haze_contact_list_remove_group_async);
    }
  else
    {
      g_simple_async_report_gerror_in_idle ((GObject *) cl, callback,
          user_data, error);
      g_clear_error (&error);
    }

  tp_handle_set_destroy (members);
}

static void
haze_contact_list_set_group_members_async (TpBaseContactList *cl,
    const gchar *group_name,
    TpHandleSet *contacts,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  HazeContactList *self = HAZE_CONTACT_LIST (cl);
  TpHandleSet *outcasts = haze_contact_list_dup_group_members (cl, group_name);
  GError *error = NULL;
  /* This actually has "ensure" semantics, and doesn't return a ref.
   * We do this even if there are no contacts, to create the group as a
   * side-effect. */
  PurpleGroup *group = purple_group_new (group_name);

  /* We have to reassure the TpBaseContactList that the group exists,
   * because libpurple doesn't have a group-added signal */
  g_assert (group != NULL);
  tp_base_contact_list_groups_created ((TpBaseContactList *) self,
      &group_name, 1);

  tp_intset_destroy (tp_handle_set_difference_update (outcasts,
        tp_handle_set_peek (contacts)));

  if (haze_contact_list_prep_remove_from_group (HAZE_CONTACT_LIST (cl),
        group_name, outcasts, &error))
    {
      haze_contact_list_add_to_group_async (cl, group_name, contacts, callback,
          user_data);
      haze_contact_list_remove_many_from_group (self, group_name, outcasts);
    }
  else
    {
      g_simple_async_report_gerror_in_idle ((GObject *) cl, callback,
          user_data, error);
      g_clear_error (&error);
    }
}

static void
haze_contact_list_rename_group_async (TpBaseContactList *cl,
    const gchar *old_name,
    const gchar *new_name,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  PurpleGroup *group = purple_find_group (old_name);
  PurpleGroup *other = purple_find_group (new_name);

  if (group == NULL)
    {
      g_simple_async_report_error_in_idle ((GObject *) cl, callback,
          user_data, TP_ERROR, TP_ERROR_DOES_NOT_EXIST,
          "The group '%s' does not exist", old_name);
      return;
    }

  if (other != NULL)
    {
      g_simple_async_report_error_in_idle ((GObject *) cl, callback,
          user_data, TP_ERROR, TP_ERROR_NOT_AVAILABLE,
          "The group '%s' already exists", new_name);
      return;
    }

  purple_blist_rename_group (group, new_name);
  tp_base_contact_list_group_renamed (cl, old_name, new_name);

  tp_simple_async_report_success_in_idle ((GObject *) cl, callback,
      user_data, haze_contact_list_rename_group_async);
}

static void
haze_contact_list_mutable_groups_init (
    TpMutableContactGroupListInterface *vtable)
{
  /* we use the default _finish functions, which assume a GSimpleAsyncResult */
  vtable->set_contact_groups_async =
    haze_contact_list_set_contact_groups_async;
  vtable->set_group_members_async = haze_contact_list_set_group_members_async;
  vtable->add_to_group_async = haze_contact_list_add_to_group_async;
  vtable->remove_from_group_async = haze_contact_list_remove_from_group_async;
  vtable->remove_group_async = haze_contact_list_remove_group_async;
  vtable->rename_group_async = haze_contact_list_rename_group_async;
  /* assume default: groups are stored persistently */
}

static TpHandleSet *
dup_blocked_contacts (TpBaseContactList *cl)
{
  HazeContactList *self = HAZE_CONTACT_LIST (cl);
  PurpleAccount *account = self->priv->conn->account;
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (self->priv->conn);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);
  TpHandleSet *blocked = tp_handle_set_new (contact_repo);
  GSList *l;

  for (l = account->deny; l != NULL; l = l->next) {
    TpHandle handle = tp_handle_ensure (contact_repo, l->data, NULL, NULL);

    if (G_LIKELY (handle != 0))
      tp_handle_set_add (blocked, handle);
  }

  return blocked;
}

static void
set_contacts_privacy (TpBaseContactList *cl,
    TpHandleSet *contacts,
    gboolean block)
{
  HazeContactList *self = HAZE_CONTACT_LIST (cl);
  PurpleAccount *account = self->priv->conn->account;
  TpIntsetFastIter iter;
  TpHandle handle;

  tp_intset_fast_iter_init (&iter, tp_handle_set_peek (contacts));

  while (tp_intset_fast_iter_next (&iter, &handle))
    {
      const gchar *bname = haze_connection_handle_inspect (self->priv->conn,
          TP_HANDLE_TYPE_CONTACT, handle);

      if (block)
        purple_privacy_deny (account, bname, FALSE, FALSE);
      else
        purple_privacy_allow (account, bname, FALSE, FALSE);
    }
}

static void
block_contacts_async(TpBaseContactList *cl,
    TpHandleSet *contacts,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  set_contacts_privacy (cl, contacts, TRUE);

  tp_simple_async_report_success_in_idle ((GObject *) cl, callback,
      user_data, block_contacts_async);
}

static void
unblock_contacts_async(TpBaseContactList *cl,
    TpHandleSet *contacts,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  set_contacts_privacy (cl, contacts, FALSE);

  tp_simple_async_report_success_in_idle ((GObject *) cl, callback,
      user_data, unblock_contacts_async);
}

static gboolean
can_block (TpBaseContactList *cl)
{
  HazeContactList *self = HAZE_CONTACT_LIST (cl);

  return (self->priv->conn->account->gc != NULL &&
      HAZE_CONNECTION_GET_PRPL_INFO (self->priv->conn)->add_deny != NULL);
}

static void
haze_contact_list_blockable_init(
    TpBlockableContactListInterface *vtable)
{
  vtable->dup_blocked_contacts = dup_blocked_contacts;
  vtable->block_contacts_async = block_contacts_async;
  vtable->unblock_contacts_async = unblock_contacts_async;

  vtable->can_block = can_block;
}

static void
haze_contact_list_deny_changed (
    PurpleAccount *account,
    const char *name)
{
  HazeConnection *conn = ACCOUNT_GET_HAZE_CONNECTION (account);
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
  TpHandleRepoIface *contact_repo = tp_base_connection_get_handles (base_conn,
      TP_HANDLE_TYPE_CONTACT);
  GError *error = NULL;
  TpHandle handle = tp_handle_ensure (contact_repo, name, NULL, &error);
  TpHandleSet *set;

  if (handle == 0)
    {
      g_warning ("Couldn't normalize id '%s': '%s'", name, error->message);
      g_clear_error (&error);
      return;
    }

  set = tp_handle_set_new_containing (contact_repo, handle);
  tp_base_contact_list_contact_blocking_changed (
      TP_BASE_CONTACT_LIST (conn->contact_list),
      set);
  g_object_unref (set);
}

static PurplePrivacyUiOps privacy_ui_ops =
{
  /* .permit_added = */ NULL,
  /* .permit_removed = */ NULL,
  /* .deny_added = */ haze_contact_list_deny_changed,
  /* .deny_removed = */ haze_contact_list_deny_changed
};

PurplePrivacyUiOps *
haze_get_privacy_ui_ops (void)
{
  return &privacy_ui_ops;
}
