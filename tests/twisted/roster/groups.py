"""
Test adding to, and removing from, groups
"""

import dbus

from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import domish, xpath

from servicetest import (EventPattern, wrap_channel, assertLength,
        assertEquals, call_async, sync_dbus, assertContains, assertSameSets)
from hazetest import acknowledge_iq, exec_test, sync_stream
import constants as cs
import ns

# TODO: this needs porting to ContactList
raise SystemExit(77)

def test(q, bus, conn, stream):
    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")

    romeo, juliet, duncan = conn.get_contact_handles_sync(
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

    # the XMPP prpl puts people into some sort of group, probably called
    # Buddies
    groups = conn.Properties.Get(cs.CONN_IFACE_CONTACT_GROUPS, 'Groups')
    default_group = None

    for group in groups:
        if group in ('Capulets', 'Still alive'):
            continue

        if default_group is not None:
            raise AssertionError('Two unexplained groups: %s, %s' %
                    (group, default_group))

        default_group = group

    call_async(q, conn.ContactList, 'GetContactListAttributes',
            [cs.CONN_IFACE_CONTACT_GROUPS], False)
    r = q.expect('dbus-return', method='GetContactListAttributes')

    assertSameSets(['Still alive'], r.value[0][romeo][cs.ATTR_GROUPS])
    assertSameSets(['Still alive', 'Capulets'],
            r.value[0][juliet][cs.ATTR_GROUPS])
    assertSameSets([default_group], r.value[0][duncan][cs.ATTR_GROUPS])

    # We can't remove Duncan from the default group, because it's his only
    # group
    call_async(q, conn.ContactGroups, 'RemoveFromGroup', default_group,
            [duncan])
    q.expect('dbus-error', method='RemoveFromGroup',
            name=cs.NOT_AVAILABLE)
    call_async(q, conn.ContactGroups, 'SetGroupMembers', default_group,
            [])
    q.expect('dbus-error', method='SetGroupMembers',
            name=cs.NOT_AVAILABLE)
    # SetContactGroups just doesn't do anything in this situation
    call_async(q, conn.ContactGroups, 'SetContactGroups', duncan, [])
    q.expect('dbus-return', method='SetContactGroups')

    call_async(q, conn.ContactList, 'GetContactListAttributes',
            [cs.CONN_IFACE_CONTACT_GROUPS], False)
    r = q.expect('dbus-return', method='GetContactListAttributes')
    assertSameSets([default_group], r.value[0][duncan][cs.ATTR_GROUPS])

    # Make a new group and add Duncan to it
    call_async(q, conn.ContactGroups, 'AddToGroup', 'Scots', [duncan])
    iq, _, _ = q.expect_many(
            EventPattern('stream-iq', iq_type='set', query_name='query',
                query_ns=ns.ROSTER),
            EventPattern('dbus-signal', signal='GroupsChanged',
                args=[[duncan], ['Scots'], []]),
            EventPattern('dbus-return', method='AddToGroup'),
            )
    assertEquals('duncan@scotland.lit', iq.stanza.query.item['jid'])
    groups = {str(x) for x in xpath.queryForNodes('/iq/query/item/group',
        iq.stanza)}
    assertLength(2, groups)
    assertContains(default_group, groups)
    assertContains('Scots', groups)

    # Now we can remove him from the default group. Much rejoicing.
    call_async(q, conn.ContactGroups, 'RemoveFromGroup', default_group,
            [duncan])
    iq, _, _ = q.expect_many(
            EventPattern('stream-iq', iq_type='set', query_name='query',
                query_ns=ns.ROSTER),
            EventPattern('dbus-signal', signal='GroupsChanged',
                args=[[duncan], [], [default_group]]),
            EventPattern('dbus-return', method='RemoveFromGroup'),
            )
    assertEquals('duncan@scotland.lit', iq.stanza.query.item['jid'])
    groups = {str(x) for x in xpath.queryForNodes('/iq/query/item/group',
        iq.stanza)}
    assertLength(1, groups)
    assertContains('Scots', groups)

    # Test SetContactGroups, which didn't previously have proper coverage
    call_async(q, conn.ContactGroups, 'SetContactGroups', duncan,
            ['Scottish former kings'])
    iq, _, _, _ = q.expect_many(
            EventPattern('stream-iq', iq_type='set', query_name='query',
                query_ns=ns.ROSTER),
            EventPattern('dbus-signal', signal='GroupsChanged',
                args=[[duncan], ['Scottish former kings'], []]),
            EventPattern('dbus-signal', signal='GroupsChanged',
                args=[[duncan], [], ['Scots']]),
            EventPattern('dbus-return', method='SetContactGroups'),
            )
    assertEquals('duncan@scotland.lit', iq.stanza.query.item['jid'])
    groups = {str(x) for x in xpath.queryForNodes('/iq/query/item/group',
        iq.stanza)}
    assertLength(2, groups)
    assertContains('Scots', groups)
    assertContains('Scottish former kings', groups)
    iq, = q.expect_many(
            EventPattern('stream-iq', iq_type='set', query_name='query',
                query_ns=ns.ROSTER),
            )
    assertEquals('duncan@scotland.lit', iq.stanza.query.item['jid'])
    groups = {str(x) for x in xpath.queryForNodes('/iq/query/item/group',
        iq.stanza)}
    assertLength(1, groups)
    assertContains('Scottish former kings', groups)

    # Romeo dies. If he drops off the roster as a result, that would be
    # fd.o #21294. However, to fix that bug, Haze now puts him in the
    # default group.
    call_async(q, conn.ContactGroups, 'RemoveFromGroup', 'Still alive',
            [romeo])
    iq1, iq2, _, _, _ = q.expect_many(
            EventPattern('stream-iq', iq_type='set', query_name='query',
                query_ns=ns.ROSTER),
            EventPattern('stream-iq', iq_type='set', query_name='query',
                query_ns=ns.ROSTER),
            EventPattern('dbus-signal', signal='GroupsChanged',
                args=[[romeo], [default_group], []]),
            EventPattern('dbus-signal', signal='GroupsChanged',
                args=[[romeo], [], ['Still alive']]),
            EventPattern('dbus-return', method='RemoveFromGroup'),
            )

    assertEquals('romeo@montague.lit', iq1.stanza.query.item['jid'])
    groups = {str(x) for x in xpath.queryForNodes('/iq/query/item/group',
        iq1.stanza)}
    assertLength(2, groups)
    assertContains('Still alive', groups)
    assertContains(default_group, groups)

    assertEquals('romeo@montague.lit', iq2.stanza.query.item['jid'])
    groups = {str(x) for x in xpath.queryForNodes('/iq/query/item/group',
        iq2.stanza)}
    assertLength(1, groups)
    assertContains(default_group, groups)

    # Juliet dies. She's in another group already, so the workaround for
    # fd.o #21294 is not active.
    call_async(q, conn.ContactGroups, 'SetGroupMembers', 'Still alive', [])
    iq, _, _ = q.expect_many(
            EventPattern('stream-iq', iq_type='set', query_name='query',
                query_ns=ns.ROSTER),
            EventPattern('dbus-signal', signal='GroupsChanged',
                args=[[juliet], [], ['Still alive']]),
            EventPattern('dbus-return', method='SetGroupMembers'),
            )
    assertEquals('juliet@capulet.lit', iq.stanza.query.item['jid'])
    groups = {str(x) for x in xpath.queryForNodes('/iq/query/item/group',
        iq.stanza)}
    assertLength(1, groups)
    assertContains('Capulets', groups)

    # At the end of a tragedy, everyone dies, so there's no need for this
    # group.
    call_async(q, conn.ContactGroups, 'RemoveGroup', 'Still alive')
    q.expect('dbus-signal', signal='GroupsRemoved', args=[['Still alive']])

    # Deleting a non-empty group is allowed. (It removes everyone.)
    call_async(q, conn.ContactGroups, 'RemoveGroup', 'Capulets')
    q.expect_many(
            EventPattern('dbus-signal', signal='GroupsRemoved',
                args=[['Capulets']]),
            EventPattern('dbus-signal', signal='GroupsChanged',
                args=[[juliet], [default_group], []]),
            EventPattern('dbus-signal', signal='GroupsChanged',
                args=[[juliet], [], ['Capulets']]),
            )

if __name__ == '__main__':
    exec_test(test)
