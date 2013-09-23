"""
Test subscribing to a contact's presence.
"""

import dbus

from twisted.words.xish import domish

from servicetest import (EventPattern, wrap_channel, assertLength,
        assertEquals, call_async, sync_dbus)
from hazetest import acknowledge_iq, exec_test, sync_stream
import constants as cs
import ns

def test(q, bus, conn, stream):
    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")

    call_async(q, conn.ContactList, 'GetContactListAttributes',
            [cs.CONN_IFACE_CONTACT_GROUPS], False)
    r = q.expect('dbus-return', method='GetContactListAttributes')
    assertLength(0, r.value[0].keys())

    # request subscription
    handle = conn.get_contact_handle_sync('suggs@night.boat.cairo')
    call_async(q, conn.ContactList, 'RequestSubscription', [handle],
            'half past monsoon')

    # libpurple puts him on our blist as soon as we've asked; there doesn't
    # seem to be any concept of remote-pending state.
    #
    # It also puts him in the default group, probably "Buddies".
    set_iq, _, _, _, groups_changed = q.expect_many(
            EventPattern('stream-iq', iq_type='set',
                query_ns=ns.ROSTER, query_name='query'),
            EventPattern('stream-presence', presence_type='subscribe',
                to='suggs@night.boat.cairo'),
            EventPattern('dbus-return', method='RequestSubscription', value=()),
            EventPattern('dbus-signal', signal='ContactsChangedWithID',
                args=[{
                    handle:
                        (cs.SUBSCRIPTION_STATE_YES,
                            cs.SUBSCRIPTION_STATE_UNKNOWN, ''),
                    },
                    {handle: 'suggs@night.boat.cairo'}, {}]),
            EventPattern('dbus-signal', signal='GroupsChanged',
                predicate=lambda e: e.args[0] == [handle]),
            )

    assertEquals('suggs@night.boat.cairo', set_iq.query.item['jid'])
    acknowledge_iq(stream, set_iq.stanza)

    assertLength(1, groups_changed.args[1])
    assertLength(0, groups_changed.args[2])
    def_group = groups_changed.args[1][0]

    # Suggs accepts our subscription request
    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = 'suggs@night.boat.cairo'
    presence['type'] = 'subscribed'
    stream.send(presence)

    # ... but nothing much happens, because there's no concept of pending
    # state in libpurple

    # put a contact into the *group* explicitly: this shouldn't ask for
    # subscription, but it does, because libpurple
    handle = conn.get_contact_handle_sync('ayria@revenge.world')
    call_async(q, conn.ContactGroups, 'AddToGroup', def_group, [handle])

    # libpurple puts her on our blist as soon as we've asked; there doesn't
    # seem to be any concept of remote-pending state. It also puts her in the
    # same group.
    set_iq, _, _, _, _ = q.expect_many(
            EventPattern('stream-iq', iq_type='set',
                query_ns=ns.ROSTER, query_name='query'),
            EventPattern('stream-presence', presence_type='subscribe',
                to='ayria@revenge.world'),
            EventPattern('dbus-return', method='AddToGroup', value=()),
            EventPattern('dbus-signal', signal='ContactsChangedWithID',
                args=[{
                    handle:
                        (cs.SUBSCRIPTION_STATE_YES,
                            cs.SUBSCRIPTION_STATE_UNKNOWN, ''),
                    },
                    {handle: 'ayria@revenge.world'}, {}]),
            EventPattern('dbus-signal', signal='GroupsChanged',
                args=[[handle], [def_group], []]),
            )

    acknowledge_iq(stream, set_iq.stanza)
    assertEquals('ayria@revenge.world', set_iq.query.item['jid'])

    # cybergoths are less receptive to random subscription requests, so it
    # gets rejected
    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = 'ayria@revenge.world'
    presence['type'] = 'unsubscribed'
    stream.send(presence)

    # nothing happens, because there's no concept of pending state...

    sync_dbus(bus, q, conn)
    sync_stream(q, stream)

if __name__ == '__main__':
    exec_test(test)
