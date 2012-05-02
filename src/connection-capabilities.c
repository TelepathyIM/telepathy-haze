/*
 * connection-capabilities.c - Capabilities interface implementation of HazeConnection
 * Copyright (C) 2005, 2006, 2008, 2009 Collabora Ltd.
 * Copyright (C) 2005, 2006, 2008 Nokia Corporation
 *
 * Copied heavily from telepathy-gabble
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

#include "connection-capabilities.h"

#include <telepathy-glib/contacts-mixin.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/handle.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/dbus.h>

#include "connection.h"
#include "debug.h"
#ifdef ENABLE_MEDIA
#include "mediamanager.h"
#endif

static void
free_rcc_list (GPtrArray *rccs)
{
  g_boxed_free (TP_ARRAY_TYPE_REQUESTABLE_CHANNEL_CLASS_LIST, rccs);
}

#ifdef ENABLE_MEDIA
static PurpleMediaCaps
tp_flags_to_purple_caps (guint flags)
{
  PurpleMediaCaps caps = PURPLE_MEDIA_CAPS_NONE;
  if (flags & TP_CHANNEL_MEDIA_CAPABILITY_AUDIO)
    caps |= PURPLE_MEDIA_CAPS_AUDIO;
  if (flags & TP_CHANNEL_MEDIA_CAPABILITY_VIDEO)
    caps |= PURPLE_MEDIA_CAPS_VIDEO;
  return caps;
}

static guint
purple_caps_to_tp_flags (PurpleMediaCaps caps)
{
  guint flags = 0;
  if (caps & PURPLE_MEDIA_CAPS_AUDIO)
    flags |= TP_CHANNEL_MEDIA_CAPABILITY_AUDIO;
  if (caps & PURPLE_MEDIA_CAPS_VIDEO)
    flags |= TP_CHANNEL_MEDIA_CAPABILITY_VIDEO;
  return flags;
}

static GPtrArray * haze_connection_get_handle_contact_capabilities (
    HazeConnection *self, TpHandle handle);

static void
_emit_capabilities_changed (HazeConnection *conn,
                            TpHandle handle,
                            const guint old_specific,
                            const guint new_specific)
{
  GPtrArray *caps_arr;
  guint i;

  /* o.f.T.C.Capabilities */

  caps_arr = g_ptr_array_new ();

  if (old_specific != 0 || new_specific != 0)
    {
      GValue caps_monster_struct = {0, };
      guint old_generic = old_specific ?
          TP_CONNECTION_CAPABILITY_FLAG_CREATE |
          TP_CONNECTION_CAPABILITY_FLAG_INVITE : 0;
      guint new_generic = new_specific ?
          TP_CONNECTION_CAPABILITY_FLAG_CREATE |
          TP_CONNECTION_CAPABILITY_FLAG_INVITE : 0;

      if (0 != (old_specific ^ new_specific))
        {
          g_value_init (&caps_monster_struct,
              TP_STRUCT_TYPE_CAPABILITY_CHANGE);
          g_value_take_boxed (&caps_monster_struct,
              dbus_g_type_specialized_construct
              (TP_STRUCT_TYPE_CAPABILITY_CHANGE));

          dbus_g_type_struct_set (&caps_monster_struct,
              0, handle,
              1, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
              2, old_generic,
              3, new_generic,
              4, old_specific,
              5, new_specific,
              G_MAXUINT);

          g_ptr_array_add (caps_arr, g_value_get_boxed (&caps_monster_struct));
        }
    }

  if (caps_arr->len)
    tp_svc_connection_interface_capabilities_emit_capabilities_changed (
        conn, caps_arr);

  if (caps_arr->len > 0)
    {
      GHashTable *ret = g_hash_table_new_full (g_direct_hash, g_direct_equal,
          NULL, (GDestroyNotify) free_rcc_list);
      GPtrArray *arr;

      arr = haze_connection_get_handle_contact_capabilities (conn, handle);
      g_hash_table_insert (ret, GUINT_TO_POINTER (handle), arr);

      tp_svc_connection_interface_contact_capabilities_emit_contact_capabilities_changed (
          conn, ret);

      g_hash_table_unref (ret);
    }

  for (i = 0; i < caps_arr->len; i++)
    {
      g_boxed_free (TP_STRUCT_TYPE_CAPABILITY_CHANGE,
          g_ptr_array_index (caps_arr, i));
    }

  g_ptr_array_free (caps_arr, TRUE);
}
#endif

/**
 * haze_connection_advertise_capabilities
 *
 * Implements D-Bus method AdvertiseCapabilities
 * on interface org.freedesktop.Telepathy.Connection.Interface.Capabilities
 */
static void
haze_connection_advertise_capabilities (TpSvcConnectionInterfaceCapabilities *iface,
                                        const GPtrArray *add,
                                        const gchar **del,
                                        DBusGMethodInvocation *context)
{
  HazeConnection *self = HAZE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
#ifdef ENABLE_MEDIA
  guint i;
  PurpleMediaCaps old_caps, caps;
#endif
  GPtrArray *ret;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

#ifdef ENABLE_MEDIA
  caps = old_caps = purple_media_manager_get_ui_caps (
      purple_media_manager_get ());
  for (i = 0; i < add->len; i++)
    {
      GValue iface_flags_pair = {0, };
      gchar *channel_type;
      guint flags;

      g_value_init (&iface_flags_pair, TP_STRUCT_TYPE_CAPABILITY_PAIR);
      g_value_set_static_boxed (&iface_flags_pair, g_ptr_array_index (add, i));

      dbus_g_type_struct_get (&iface_flags_pair,
                              0, &channel_type,
                              1, &flags,
                              G_MAXUINT);

      if (g_str_equal (channel_type, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA))
        caps |= tp_flags_to_purple_caps(flags);

      g_free (channel_type);
    }

  for (i = 0; NULL != del[i]; i++)
    {
      if (g_str_equal (del[i], TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA))
        {
          caps = PURPLE_MEDIA_CAPS_NONE;
          break;
        }
    }

  purple_media_manager_set_ui_caps (purple_media_manager_get(), caps);

  _emit_capabilities_changed (self, base->self_handle, old_caps, caps);
#endif

  ret = g_ptr_array_new ();

/* TODO: store caps and return them properly */

  tp_svc_connection_interface_capabilities_return_from_advertise_capabilities (
      context, ret);
  g_ptr_array_free (ret, TRUE);
}

typedef enum {
  CAPS_FLAGS_AUDIO = 1 << 0,
  CAPS_FLAGS_VIDEO = 1 << 1,
} CapsFlags;

static void
haze_connection_update_capabilities (TpSvcConnectionInterfaceContactCapabilities *iface,
                                     const GPtrArray *clients,
                                     DBusGMethodInvocation *context)
{
  HazeConnection *self = HAZE_CONNECTION (iface);
  TpBaseConnection *base = (TpBaseConnection *) self;
#ifdef ENABLE_MEDIA
  guint i;
  PurpleMediaCaps old_caps, caps;
  GHashTableIter iter;
  gpointer value;
#endif

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

#ifdef ENABLE_MEDIA
  caps = PURPLE_MEDIA_CAPS_NONE;
  old_caps = purple_media_manager_get_ui_caps (
      purple_media_manager_get ());

  DEBUG ("enter");

  /* go through all the clients and if they can do audio or video save
   * it in the client_caps hash table */
  for (i = 0; i < clients->len; i++)
    {
      GValueArray *va = g_ptr_array_index (clients, i);
      const gchar *client_name = g_value_get_string (va->values + 0);
      const GPtrArray *rccs = g_value_get_boxed (va->values + 1);
      guint j;
      CapsFlags flags = 0;

      g_hash_table_remove (self->client_caps, client_name);

      for (j = 0; j < rccs->len; j++)
        {
          GHashTable *class = g_ptr_array_index (rccs, i);

          if (tp_strdiff (tp_asv_get_string (class, TP_PROP_CHANNEL_CHANNEL_TYPE),
                  TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA))
            continue;

          if (tp_asv_get_boolean (class,
                  TP_PROP_CHANNEL_TYPE_STREAMED_MEDIA_INITIAL_AUDIO, NULL))
            flags |= CAPS_FLAGS_AUDIO;

          if (tp_asv_get_boolean (class,
                  TP_PROP_CHANNEL_TYPE_STREAMED_MEDIA_INITIAL_VIDEO, NULL))
            flags |= CAPS_FLAGS_VIDEO;
        }

      if (flags != 0)
        {
          g_hash_table_insert (self->client_caps, g_strdup (client_name),
              GUINT_TO_POINTER (flags));
        }
    }

  /* now we have an updated client_caps hash table, go through it and
   * let libpurple know */
  g_hash_table_iter_init (&iter, self->client_caps);
  while (g_hash_table_iter_next (&iter, NULL, &value))
    {
      CapsFlags flags = GPOINTER_TO_UINT (value);

      if (flags & CAPS_FLAGS_AUDIO)
        caps |= PURPLE_MEDIA_CAPS_AUDIO;
      if (flags & CAPS_FLAGS_VIDEO)
        caps |= PURPLE_MEDIA_CAPS_VIDEO;
    }

  purple_media_manager_set_ui_caps (purple_media_manager_get(), caps);

  _emit_capabilities_changed (self, base->self_handle, old_caps, caps);
#endif

  tp_svc_connection_interface_contact_capabilities_return_from_update_capabilities (
      context);
}

static const gchar *assumed_caps[] =
{
  TP_IFACE_CHANNEL_TYPE_TEXT,
  NULL
};

/**
 * haze_connection_get_handle_capabilities
 *
 * Add capabilities of handle to the given GPtrArray
 */
static void
haze_connection_get_handle_capabilities (HazeConnection *self,
                                         TpHandle handle,
                                         GPtrArray *arr)
{
#ifdef ENABLE_MEDIA
  TpBaseConnection *conn = TP_BASE_CONNECTION (self);
  PurpleAccount *account = self->account;
  TpHandleRepoIface *contact_handles =
      tp_base_connection_get_handles (conn, TP_HANDLE_TYPE_CONTACT);
  const gchar *bname;
  guint typeflags = 0;
  PurpleMediaCaps caps;
#endif
  const gchar **assumed;

  if (0 == handle)
    {
      /* obsolete request for the connection's capabilities, do nothing */
      return;
    }

  /* TODO: Check for presence */

#ifdef ENABLE_MEDIA
  if (handle == conn->self_handle)
    caps = purple_media_manager_get_ui_caps (purple_media_manager_get ());
  else
    {
      bname = tp_handle_inspect (contact_handles, handle);
      caps = purple_prpl_get_media_caps (account, bname);
    }

  typeflags = purple_caps_to_tp_flags(caps);

  if (typeflags != 0)
    {
      GValue monster = {0, };
      g_value_init (&monster, TP_STRUCT_TYPE_CONTACT_CAPABILITY);
      g_value_take_boxed (&monster,
          dbus_g_type_specialized_construct (
          TP_STRUCT_TYPE_CONTACT_CAPABILITY));
      dbus_g_type_struct_set (&monster,
          0, handle,
          1, TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA,
          2, TP_CONNECTION_CAPABILITY_FLAG_CREATE |
             TP_CONNECTION_CAPABILITY_FLAG_INVITE,
          3, typeflags,
          G_MAXUINT);

      g_ptr_array_add (arr, g_value_get_boxed (&monster));
    }
#endif

  for (assumed = assumed_caps; NULL != *assumed; assumed++)
    {
      GValue monster = {0, };
      g_value_init (&monster, TP_STRUCT_TYPE_CONTACT_CAPABILITY);
      g_value_take_boxed (&monster,
          dbus_g_type_specialized_construct (
          TP_STRUCT_TYPE_CONTACT_CAPABILITY));

      dbus_g_type_struct_set (&monster,
          0, handle,
          1, *assumed,
          2, TP_CONNECTION_CAPABILITY_FLAG_CREATE |
             TP_CONNECTION_CAPABILITY_FLAG_INVITE,
          3, 0,
          G_MAXUINT);

      g_ptr_array_add (arr, g_value_get_boxed (&monster));
    }
}

static GPtrArray *
haze_connection_get_handle_contact_capabilities (HazeConnection *self,
                                                 TpHandle handle)
{
#ifdef ENABLE_MEDIA
  PurpleAccount *account = self->account;
  TpBaseConnection *conn = TP_BASE_CONNECTION (self);
  TpHandleRepoIface *contact_handles =
      tp_base_connection_get_handles (conn, TP_HANDLE_TYPE_CONTACT);
  const gchar *bname;
  PurpleMediaCaps caps;
  GValue media_monster = {0, };
  guint typeflags = 0;
  const gchar * const sm_allowed_audio[] = {
    TP_PROP_CHANNEL_TYPE_STREAMED_MEDIA_INITIAL_AUDIO, NULL };
  const gchar * const sm_allowed_video[] = {
    TP_PROP_CHANNEL_TYPE_STREAMED_MEDIA_INITIAL_AUDIO,
    TP_PROP_CHANNEL_TYPE_STREAMED_MEDIA_INITIAL_VIDEO,
    NULL };
#endif
  GPtrArray *arr = g_ptr_array_new ();
  GValue monster = {0, };
  GHashTable *fixed_properties;
  GValue *channel_type_value;
  GValue *target_handle_type_value;
  const gchar * const text_allowed_properties[] = {
    TP_PROP_CHANNEL_TARGET_HANDLE, NULL };

  if (0 == handle)
    {
      /* obsolete request for the connection's capabilities, do nothing */
      return arr;
    }

  /* TODO: Check for presence */

#ifdef ENABLE_MEDIA
  if (handle == conn->self_handle)
    caps = purple_media_manager_get_ui_caps (purple_media_manager_get ());
  else
    {
      bname = tp_handle_inspect (contact_handles, handle);
      caps = purple_prpl_get_media_caps (account, bname);
    }

  typeflags = purple_caps_to_tp_flags(caps);

  if (typeflags != 0)
    {
      const gchar * const *allowed;

      g_value_init (&media_monster,
          TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS);
      g_value_take_boxed (&media_monster,
          dbus_g_type_specialized_construct (
              TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS));

      fixed_properties = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
          (GDestroyNotify) tp_g_value_slice_free);

      channel_type_value = tp_g_value_slice_new (G_TYPE_STRING);
      g_value_set_static_string (channel_type_value,
          TP_IFACE_CHANNEL_TYPE_STREAMED_MEDIA);
      g_hash_table_insert (fixed_properties, TP_PROP_CHANNEL_CHANNEL_TYPE,
          channel_type_value);

      target_handle_type_value = tp_g_value_slice_new (G_TYPE_UINT);
      g_value_set_uint (target_handle_type_value, TP_HANDLE_TYPE_CONTACT);
      g_hash_table_insert (fixed_properties, TP_PROP_CHANNEL_TARGET_HANDLE_TYPE,
          target_handle_type_value);

      if (typeflags & TP_CHANNEL_MEDIA_CAPABILITY_VIDEO)
        allowed = sm_allowed_video;
      else
        allowed = sm_allowed_audio;

      dbus_g_type_struct_set (&media_monster,
          0, fixed_properties,
          1, allowed,
          G_MAXUINT);

      g_hash_table_unref (fixed_properties);

      g_ptr_array_add (arr, g_value_get_boxed (&media_monster));
    }
#endif

  g_value_init (&monster, TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS);
  g_value_take_boxed (&monster,
      dbus_g_type_specialized_construct (
        TP_STRUCT_TYPE_REQUESTABLE_CHANNEL_CLASS));

  fixed_properties = g_hash_table_new_full (g_str_hash, g_str_equal, NULL,
      (GDestroyNotify) tp_g_value_slice_free);

  channel_type_value = tp_g_value_slice_new (G_TYPE_STRING);
  g_value_set_static_string (channel_type_value, TP_IFACE_CHANNEL_TYPE_TEXT);
  g_hash_table_insert (fixed_properties, TP_IFACE_CHANNEL ".ChannelType",
      channel_type_value);

  target_handle_type_value = tp_g_value_slice_new (G_TYPE_UINT);
  g_value_set_uint (target_handle_type_value, TP_HANDLE_TYPE_CONTACT);
  g_hash_table_insert (fixed_properties, TP_IFACE_CHANNEL ".TargetHandleType",
      target_handle_type_value);

  dbus_g_type_struct_set (&monster,
      0, fixed_properties,
      1, text_allowed_properties,
      G_MAXUINT);

  g_hash_table_unref (fixed_properties);

  g_ptr_array_add (arr, g_value_get_boxed (&monster));

  return arr;
}

/**
 * haze_connection_get_capabilities
 *
 * Implements D-Bus method GetCapabilities
 * on interface org.freedesktop.Telepathy.Connection.Interface.Capabilities
 */
static void
haze_connection_get_capabilities (TpSvcConnectionInterfaceCapabilities *iface,
                                  const GArray *handles,
                                  DBusGMethodInvocation *context)
{
  HazeConnection *self = HAZE_CONNECTION (iface);
  TpBaseConnection *conn = TP_BASE_CONNECTION (self);
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (conn,
      TP_HANDLE_TYPE_CONTACT);
  guint i;
  GPtrArray *ret;
  GError *error = NULL;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (conn, context);

  if (!tp_handles_are_valid (contact_handles, handles, TRUE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  ret = g_ptr_array_new ();

  for (i = 0; i < handles->len; i++)
    {
      TpHandle handle = g_array_index (handles, TpHandle, i);

      haze_connection_get_handle_capabilities (self, handle, ret);
    }

  tp_svc_connection_interface_capabilities_return_from_get_capabilities (
      context, ret);

  for (i = 0; i < ret->len; i++)
    {
      g_value_array_free (g_ptr_array_index (ret, i));
    }

  g_ptr_array_free (ret, TRUE);
}

static void
conn_capabilities_fill_contact_attributes (GObject *obj,
                                           const GArray *contacts,
                                           GHashTable *attributes_hash)
{
  HazeConnection *self = HAZE_CONNECTION (obj);
  guint i;
  GPtrArray *array = NULL;

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);

      if (array == NULL)
        array = g_ptr_array_new ();

      haze_connection_get_handle_capabilities (self, handle, array);

      if (array->len > 0)
        {
          GValue *val =  tp_g_value_slice_new (
              TP_ARRAY_TYPE_CONTACT_CAPABILITY_LIST);

          g_value_take_boxed (val, array);
          tp_contacts_mixin_set_contact_attribute (attributes_hash,
              handle, TP_IFACE_CONNECTION_INTERFACE_CAPABILITIES"/caps",
              val);

          array = NULL;
        }
    }

    if (array != NULL)
      g_ptr_array_free (array, TRUE);
}

static void
conn_capabilities_fill_contact_attributes_contact_caps (
    GObject *obj,
    const GArray *contacts,
    GHashTable *attributes_hash)
{
  HazeConnection *self = HAZE_CONNECTION (obj);
  guint i;

  for (i = 0; i < contacts->len; i++)
    {
      TpHandle handle = g_array_index (contacts, TpHandle, i);
      GPtrArray *array;

      array = haze_connection_get_handle_contact_capabilities (self, handle);

      if (array->len > 0)
        {
          GValue *val = tp_g_value_slice_new (
              TP_ARRAY_TYPE_REQUESTABLE_CHANNEL_CLASS_LIST);

          g_value_take_boxed (val, array);
          tp_contacts_mixin_set_contact_attribute (attributes_hash,
              handle, TP_IFACE_CONNECTION_INTERFACE_CONTACT_CAPABILITIES "/capabilities",
              val);
        }
      else
        g_ptr_array_free (array, TRUE);
    }
}

void
haze_connection_capabilities_iface_init (gpointer g_iface,
                                         gpointer iface_data)
{
  TpSvcConnectionInterfaceCapabilitiesClass *klass = g_iface;

#define IMPLEMENT(x) \
    tp_svc_connection_interface_capabilities_implement_##x (\
    klass, haze_connection_##x)
  IMPLEMENT(advertise_capabilities);
  IMPLEMENT(get_capabilities);
#undef IMPLEMENT
}

static void
haze_connection_get_contact_capabilities (
    TpSvcConnectionInterfaceContactCapabilities *svc,
    const GArray *handles,
    DBusGMethodInvocation *context)
{
  HazeConnection *self = HAZE_CONNECTION (svc);
  TpBaseConnection *base = (TpBaseConnection *) self;
  TpHandleRepoIface *contact_handles = tp_base_connection_get_handles (base,
      TP_HANDLE_TYPE_CONTACT);
  guint i;
  GHashTable *ret;
  GError *error = NULL;

  TP_BASE_CONNECTION_ERROR_IF_NOT_CONNECTED (base, context);

  if (!tp_handles_are_valid (contact_handles, handles, FALSE, &error))
    {
      dbus_g_method_return_error (context, error);
      g_error_free (error);
      return;
    }

  ret = g_hash_table_new_full (NULL, NULL, NULL,
      (GDestroyNotify) free_rcc_list);

  for (i = 0; i < handles->len; i++)
    {
      TpHandle handle = g_array_index (handles, TpHandle, i);
      GPtrArray *arr;

      arr = haze_connection_get_handle_contact_capabilities (self, handle);
      g_hash_table_insert (ret, GUINT_TO_POINTER (handle), arr);
    }

  tp_svc_connection_interface_contact_capabilities_return_from_get_contact_capabilities
      (context, ret);

  g_hash_table_unref (ret);
}

void
haze_connection_contact_capabilities_iface_init (gpointer g_iface,
                                                 gpointer iface_data)
{
  TpSvcConnectionInterfaceContactCapabilitiesClass *klass = g_iface;

#define IMPLEMENT(x) \
    tp_svc_connection_interface_contact_capabilities_implement_##x (\
    klass, haze_connection_##x)
  IMPLEMENT(update_capabilities);
  IMPLEMENT(get_contact_capabilities);
#undef IMPLEMENT
}

#ifdef ENABLE_MEDIA
static void
caps_changed_cb (PurpleBuddy *buddy,
                 PurpleMediaCaps caps,
                 PurpleMediaCaps oldcaps)
{
  PurpleAccount *account = purple_buddy_get_account (buddy);
  HazeConnection *conn = ACCOUNT_GET_HAZE_CONNECTION (account);
  TpBaseConnection *base_conn = TP_BASE_CONNECTION (conn);
  TpHandleRepoIface *contact_repo =
      tp_base_connection_get_handles (base_conn, TP_HANDLE_TYPE_CONTACT);
  const gchar *bname = purple_buddy_get_name(buddy);
  TpHandle contact = tp_handle_ensure (contact_repo, bname, NULL, NULL);

  _emit_capabilities_changed (conn, contact,
      purple_caps_to_tp_flags(oldcaps),
      purple_caps_to_tp_flags(caps));
}
#endif

void
haze_connection_capabilities_class_init (GObjectClass *object_class)
{
#ifdef ENABLE_MEDIA
  purple_signal_connect (purple_blist_get_handle (), "buddy-caps-changed",
      object_class, PURPLE_CALLBACK (caps_changed_cb), NULL);
#endif
}

void
haze_connection_capabilities_init (GObject *object)
{
  HazeConnection *self = HAZE_CONNECTION (object);

  tp_contacts_mixin_add_contact_attributes_iface (object,
      TP_IFACE_CONNECTION_INTERFACE_CAPABILITIES,
      conn_capabilities_fill_contact_attributes);
  tp_contacts_mixin_add_contact_attributes_iface (object,
      TP_IFACE_CONNECTION_INTERFACE_CONTACT_CAPABILITIES,
      conn_capabilities_fill_contact_attributes_contact_caps);

  self->client_caps = g_hash_table_new_full (g_str_hash, g_str_equal,
      (GDestroyNotify) g_free, NULL);
}

void
haze_connection_capabilities_finalize (GObject *object)
{
  HazeConnection *self = HAZE_CONNECTION (object);

  tp_clear_pointer (&self->client_caps, g_hash_table_unref);
}
