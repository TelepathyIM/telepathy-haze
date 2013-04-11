"""
Test adding to, and removing from, groups
"""

import dbus

from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import domish, xpath

from servicetest import (EventPattern, wrap_channel, assertLength,
        assertEquals, call_async, sync_dbus, assertContains)
from hazetest import acknowledge_iq, exec_test, sync_stream, close_all_groups
import constants as cs
import ns

def test(q, bus, conn, stream):
    self_handle = conn.GetSelfHandle()

    # Close all Group channels to get a clean slate, so we can rely on
    # the NewChannels signal for the default group later
    close_all_groups(q, bus, conn, stream)

    call_async(q, conn.Requests, 'EnsureChannel',{
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CONTACT_LIST,
        cs.TARGET_HANDLE_TYPE: cs.HT_LIST,
        cs.TARGET_ID: 'subscribe',
        })
    e = q.expect('dbus-return', method='EnsureChannel')
    subscribe = wrap_channel(bus.get_object(conn.bus_name, e.value[1]),
            cs.CHANNEL_TYPE_CONTACT_LIST)

    romeo, juliet, duncan = conn.RequestHandles(cs.HT_CONTACT,
            ['romeo@montague.lit', 'juliet@capulet.lit',
                'duncan@scotland.lit'])

    # receive some roster pushes for the "initial" state
    iq = IQ(stream, 'set')
    iq['id'] = 'roster-push'
    query = iq.addElement(('jabber:iq:roster', 'query'))
    item = query.addElement('item')
    item['jid'] = 'juliet@capulet.lit'
    item['subscription'] = 'both'
    group = item.addElement('group', content='Still alive')
    group = item.addElement('group', content='Capulets')
    stream.send(iq)

    iq = IQ(stream, 'set')
    iq['id'] = 'roster-push'
    query = iq.addElement(('jabber:iq:roster', 'query'))
    item = query.addElement('item')
    item['jid'] = 'romeo@montague.lit'
    item['subscription'] = 'both'
    group = item.addElement('group', content='Still alive')
    stream.send(iq)

    iq = IQ(stream, 'set')
    iq['id'] = 'roster-push'
    query = iq.addElement(('jabber:iq:roster', 'query'))
    item = query.addElement('item')
    item['jid'] = 'duncan@scotland.lit'
    item['subscription'] = 'both'
    stream.send(iq)

    sync_dbus(bus, q, conn)
    sync_stream(q, stream)

    call_async(q, conn.Requests, 'EnsureChannel',{
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CONTACT_LIST,
        cs.TARGET_HANDLE_TYPE: cs.HT_GROUP,
        cs.TARGET_ID: 'Still alive',
        })
    e = q.expect('dbus-return', method='EnsureChannel')
    still_alive = wrap_channel(bus.get_object(conn.bus_name, e.value[1]),
            cs.CHANNEL_TYPE_CONTACT_LIST)

    call_async(q, conn.Requests, 'EnsureChannel',{
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CONTACT_LIST,
        cs.TARGET_HANDLE_TYPE: cs.HT_GROUP,
        cs.TARGET_ID: 'Capulets',
        })
    e = q.expect('dbus-return', method='EnsureChannel')
    capulets = wrap_channel(bus.get_object(conn.bus_name, e.value[1]),
            cs.CHANNEL_TYPE_CONTACT_LIST)

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

        if props.get(cs.TARGET_ID) in ('Capulets', 'Still alive'):
            continue

        if default_group is not None:
            raise AssertionError('Two unexplained groups: %s, %s' %
                    (path, default_group.object_path))

        default_group = wrap_channel(bus.get_object(conn.bus_name, path),
                cs.CHANNEL_TYPE_CONTACT_LIST)
        default_group_name = props.get(cs.TARGET_ID)

    assertEquals(set([romeo, juliet]), set(still_alive.Group.GetMembers()))
    assertEquals(set([juliet]), set(capulets.Group.GetMembers()))
    assertEquals(set([duncan]), set(default_group.Group.GetMembers()))

    # We can't remove Duncan from the default group, because it's his only
    # group
    call_async(q, default_group.Group, 'RemoveMembers', [duncan], '')
    q.expect('dbus-error', method='RemoveMembers',
            name=cs.NOT_AVAILABLE)

    # Make a new group and add Duncan to it
    call_async(q, conn.Requests, 'CreateChannel',{
        cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_CONTACT_LIST,
        cs.TARGET_HANDLE_TYPE: cs.HT_GROUP,
        cs.TARGET_ID: 'Scots',
        })
    e = q.expect('dbus-return', method='CreateChannel')
    scots = wrap_channel(bus.get_object(conn.bus_name, e.value[0]),
            cs.CHANNEL_TYPE_CONTACT_LIST)
    assertEquals(set(), set(scots.Group.GetMembers()))

    call_async(q, scots.Group, 'AddMembers', [duncan], '')
    iq, _, _ = q.expect_many(
            EventPattern('stream-iq', iq_type='set', query_name='query',
                query_ns=ns.ROSTER),
            EventPattern('dbus-signal', signal='MembersChanged',
                path=scots.object_path,
                args=['', [duncan], [], [], [], self_handle, cs.GC_REASON_NONE]),
            EventPattern('dbus-return', method='AddMembers'),
            )
    assertEquals('duncan@scotland.lit', iq.stanza.query.item['jid'])
    groups = set([str(x) for x in xpath.queryForNodes('/iq/query/item/group',
        iq.stanza)])
    assertLength(2, groups)
    assertContains(default_group_name, groups)
    assertContains('Scots', groups)

    # Now we can remove him from the default group. Much rejoicing.
    call_async(q, default_group.Group, 'RemoveMembers', [duncan], '')
    iq, _, _ = q.expect_many(
            EventPattern('stream-iq', iq_type='set', query_name='query',
                query_ns=ns.ROSTER),
            EventPattern('dbus-signal', signal='MembersChanged',
                path=default_group.object_path,
                args=['', [], [duncan], [], [], self_handle, cs.GC_REASON_NONE]),
            EventPattern('dbus-return', method='RemoveMembers'),
            )
    assertEquals('duncan@scotland.lit', iq.stanza.query.item['jid'])
    groups = set([str(x) for x in xpath.queryForNodes('/iq/query/item/group',
        iq.stanza)])
    assertLength(1, groups)
    assertContains('Scots', groups)

    # Romeo dies. If he drops off the roster as a result, that would be
    # fd.o #21294. However, to fix that bug, Haze now puts him in the
    # default group.
    call_async(q, still_alive.Group, 'RemoveMembers', [romeo], '')
    iq1, iq2, _, _, _ = q.expect_many(
            EventPattern('stream-iq', iq_type='set', query_name='query',
                query_ns=ns.ROSTER),
            EventPattern('stream-iq', iq_type='set', query_name='query',
                query_ns=ns.ROSTER),
            EventPattern('dbus-signal', signal='MembersChanged',
                path=still_alive.object_path,
                args=['', [], [romeo], [], [], self_handle, cs.GC_REASON_NONE]),
            EventPattern('dbus-signal', signal='MembersChanged',
                path=default_group.object_path,
                args=['', [romeo], [], [], [], self_handle, cs.GC_REASON_NONE]),
            EventPattern('dbus-return', method='RemoveMembers'),
            )

    assertEquals('romeo@montague.lit', iq1.stanza.query.item['jid'])
    groups = set([str(x) for x in xpath.queryForNodes('/iq/query/item/group',
        iq1.stanza)])
    assertLength(2, groups)
    assertContains('Still alive', groups)
    assertContains(default_group_name, groups)

    assertEquals('romeo@montague.lit', iq2.stanza.query.item['jid'])
    groups = set([str(x) for x in xpath.queryForNodes('/iq/query/item/group',
        iq2.stanza)])
    assertLength(1, groups)
    assertContains(default_group_name, groups)

    # Juliet dies. She's in another group already, so the workaround for
    # fd.o #21294 is not active.
    call_async(q, still_alive.Group, 'RemoveMembers', [juliet], '')
    iq, _, _ = q.expect_many(
            EventPattern('stream-iq', iq_type='set', query_name='query',
                query_ns=ns.ROSTER),
            EventPattern('dbus-signal', signal='MembersChanged',
                path=still_alive.object_path,
                args=['', [], [juliet], [], [], self_handle, cs.GC_REASON_NONE]),
            EventPattern('dbus-return', method='RemoveMembers'),
            )
    assertEquals('juliet@capulet.lit', iq.stanza.query.item['jid'])
    groups = set([str(x) for x in xpath.queryForNodes('/iq/query/item/group',
        iq.stanza)])
    assertLength(1, groups)
    assertContains('Capulets', groups)

    # At the end of a tragedy, everyone dies, so there's no need for this
    # group.
    call_async(q, still_alive, 'Close')
    q.expect('dbus-signal', signal='Closed', path=still_alive.object_path)
    q.expect('dbus-return', method='Close')

    # Deleting a non-empty group is not allowed.
    call_async(q, capulets, 'Close')
    q.expect('dbus-error', method='Close', name=cs.NOT_AVAILABLE)

    # Neither is deleting a List channel.
    call_async(q, subscribe, 'Close')
    q.expect('dbus-error', method='Close', name=cs.NOT_IMPLEMENTED)

if __name__ == '__main__':
    exec_test(test)
