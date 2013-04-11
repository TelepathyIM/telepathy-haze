"""
Test basic roster functionality.
"""

import dbus

from hazetest import exec_test, JabberXmlStream
from servicetest import (assertLength, EventPattern, wrap_channel,
        assertEquals, call_async)
import constants as cs
import ns

def test(q, bus, conn, stream):
    conn.Connect()

    # This test can't be exactly like Gabble's because libpurple doesn't
    # signal that it's connected until it receives a roster; as a result,
    # the publish and subscribe channels already exist on startup.

    q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged',
                args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED]),
            EventPattern('stream-authenticated'),
            )

    event = q.expect('stream-iq', query_ns=ns.ROSTER)
    event.stanza['type'] = 'result'

    item = event.query.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'
    group = item.addElement('group', content='3 letter names')

    item = event.query.addElement('item')
    item['jid'] = 'bob@foo.com'
    item['subscription'] = 'from'
    group = item.addElement('group', content='3 letter names')

    item = event.query.addElement('item')
    item['jid'] = 'chris@foo.com'
    item['subscription'] = 'to'

    stream.send(event.stanza)

    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    # Amy, Bob and Chris are all stored on our server-side roster
    call_async(q, conn.Requests, 'EnsureChannel',{
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CONTACT_LIST,
        cs.TARGET_HANDLE_TYPE: cs.HT_LIST,
        cs.TARGET_ID: 'stored',
        })
    e = q.expect('dbus-return', method='EnsureChannel')
    stored = wrap_channel(bus.get_object(conn.bus_name, e.value[1]),
            cs.CHANNEL_TYPE_CONTACT_LIST)
    jids = set(conn.InspectHandles(cs.HT_CONTACT, stored.Group.GetMembers()))
    assertEquals(set(['amy@foo.com', 'bob@foo.com', 'chris@foo.com']), jids)

    call_async(q, conn.Requests, 'EnsureChannel',{
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CONTACT_LIST,
        cs.TARGET_HANDLE_TYPE: cs.HT_LIST,
        cs.TARGET_ID: 'subscribe',
        })
    e = q.expect('dbus-return', method='EnsureChannel')
    subscribe = wrap_channel(bus.get_object(conn.bus_name, e.value[1]),
            cs.CHANNEL_TYPE_CONTACT_LIST)
    jids = set(conn.InspectHandles(cs.HT_CONTACT, subscribe.Group.GetMembers()))
    # everyone on our roster is (falsely!) alleged to be on 'subscribe'
    # (in fact this ought to be just Amy and Chris, but libpurple apparently
    # can't represent this)
    assertEquals(set(['amy@foo.com', 'bob@foo.com', 'chris@foo.com']), jids)

    call_async(q, conn.Requests, 'EnsureChannel',{
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CONTACT_LIST,
        cs.TARGET_HANDLE_TYPE: cs.HT_LIST,
        cs.TARGET_ID: 'publish',
        })
    e = q.expect('dbus-return', method='EnsureChannel')
    publish = wrap_channel(bus.get_object(conn.bus_name, e.value[1]),
            cs.CHANNEL_TYPE_CONTACT_LIST)
    jids = set(conn.InspectHandles(cs.HT_CONTACT, publish.Group.GetMembers()))
    # the publish list is somewhat imaginary because libpurple doesn't have
    # state-recovery
    assertEquals(set(), jids)

    call_async(q, conn.Requests, 'EnsureChannel',{
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CONTACT_LIST,
        cs.TARGET_HANDLE_TYPE: cs.HT_GROUP,
        cs.TARGET_ID: '3 letter names',
        })
    e = q.expect('dbus-return', method='EnsureChannel')
    group_chan = wrap_channel(bus.get_object(conn.bus_name, e.value[1]),
            cs.CHANNEL_TYPE_CONTACT_LIST)
    jids = set(conn.InspectHandles(cs.HT_CONTACT,
        group_chan.Group.GetMembers()))
    assertEquals(set(['amy@foo.com', 'bob@foo.com']), jids)

    # the XMPP prpl puts people into some sort of group, probably called
    # Buddies
    channels = conn.Properties.Get(cs.CONN_IFACE_REQUESTS, 'Channels')
    default_group = None
    default_props = None

    for path, props in channels:
        if props.get(cs.CHANNEL_TYPE) != cs.CHANNEL_TYPE_CONTACT_LIST:
            continue

        if props.get(cs.TARGET_HANDLE_TYPE) != cs.HT_GROUP:
            continue

        if path == group_chan.object_path:
            continue

        if default_group is not None:
            raise AssertionError('Two unexplained groups: %s, %s' %
                    (path, default_group.object_path))

        default_group = wrap_channel(bus.get_object(conn.bus_name, path),
                cs.CHANNEL_TYPE_CONTACT_LIST)
        default_props = props

    jids = set(conn.InspectHandles(cs.HT_CONTACT,
        default_group.Group.GetMembers()))
    assertEquals(set(['chris@foo.com']), jids)

    call_async(q, conn.Requests, 'EnsureChannel',{
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CONTACT_LIST,
        cs.TARGET_HANDLE_TYPE: cs.HT_GROUP,
        cs.TARGET_ID: default_props[cs.TARGET_ID],
        })
    e = q.expect('dbus-return', method='EnsureChannel')
    assertEquals(False, e.value[0])
    assertEquals(default_group.object_path, e.value[1])
    assertEquals(default_props, e.value[2])

if __name__ == '__main__':
    exec_test(test, protocol=JabberXmlStream, do_connect=False)
