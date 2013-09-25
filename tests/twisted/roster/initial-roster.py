"""
Test basic roster functionality.
"""

import dbus

from hazetest import exec_test, JabberXmlStream
from servicetest import (assertLength, EventPattern, wrap_channel,
        assertEquals, call_async, assertSameSets)
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

    _, s, _ = q.expect_many(
            EventPattern('dbus-signal', signal='StatusChanged',
                args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED]),
            EventPattern('dbus-signal', signal='ContactsChanged',
                interface=cs.CONN_IFACE_CONTACT_LIST, path=conn.object_path),
            EventPattern('dbus-signal', signal='ContactListStateChanged',
                args=[cs.CONTACT_LIST_STATE_SUCCESS]),
            )

    amy, bob, chris = conn.get_contact_handles_sync(
            ['amy@foo.com', 'bob@foo.com', 'chris@foo.com'])

    # Amy, Bob and Chris are all stored on our server-side roster.
    #
    # Everyone on our roster is (falsely!) alleged to have subscribe=YES
    # (in fact this ought to be just Amy and Chris, because we're publishing
    # presence to Bob without being subscribed to his presence, but libpurple
    # apparently can't represent this).
    #
    # The publish value is unknown, because libpurple doesn't have
    # state-recovery.
    assertEquals([{
        amy: (cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_UNKNOWN, ''),
        bob: (cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_UNKNOWN, ''),
        chris: (cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_UNKNOWN, ''),
        },
        {
        amy: 'amy@foo.com',
        bob: 'bob@foo.com',
        chris: 'chris@foo.com',
        },
        {}], s.args)

    # the XMPP prpl puts people into some sort of group, probably called
    # Buddies
    groups = conn.Properties.Get(cs.CONN_IFACE_CONTACT_GROUPS, 'Groups')
    default_group = None

    for group in groups:
        if group == '3 letter names':
            continue

        if default_group is not None:
            raise AssertionError('Two unexplained groups: %s, %s' %
                    (group, default_group))

        default_group = group

    call_async(q, conn.ContactList, 'GetContactListAttributes',
            [cs.CONN_IFACE_CONTACT_GROUPS])
    r = q.expect('dbus-return', method='GetContactListAttributes')

    assertEquals(cs.SUBSCRIPTION_STATE_YES, r.value[0][amy][cs.ATTR_SUBSCRIBE])
    assertEquals(cs.SUBSCRIPTION_STATE_YES, r.value[0][bob][cs.ATTR_SUBSCRIBE])
    assertEquals(cs.SUBSCRIPTION_STATE_YES, r.value[0][chris][cs.ATTR_SUBSCRIBE])

    assertEquals(cs.SUBSCRIPTION_STATE_UNKNOWN, r.value[0][amy][cs.ATTR_PUBLISH])
    assertEquals(cs.SUBSCRIPTION_STATE_UNKNOWN, r.value[0][bob][cs.ATTR_PUBLISH])
    assertEquals(cs.SUBSCRIPTION_STATE_UNKNOWN, r.value[0][chris][cs.ATTR_PUBLISH])

    assertSameSets(['3 letter names'], r.value[0][amy][cs.ATTR_GROUPS])
    assertSameSets(['3 letter names'], r.value[0][bob][cs.ATTR_GROUPS])
    assertSameSets([default_group], r.value[0][chris][cs.ATTR_GROUPS])

if __name__ == '__main__':
    exec_test(test, protocol=JabberXmlStream, do_connect=False)
