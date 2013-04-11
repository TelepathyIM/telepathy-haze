"""
Test subscribing to a contact's presence.
"""

import dbus

from twisted.words.xish import domish

from servicetest import (EventPattern, wrap_channel, assertLength,
        assertEquals, call_async, sync_dbus)
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
    jids = set(conn.InspectHandles(cs.HT_CONTACT, subscribe.Group.GetMembers()))
    assertEquals(set(), jids)

    assertLength(0, subscribe.Group.GetMembers())

    # request subscription
    handle = conn.RequestHandles(cs.HT_CONTACT, ['suggs@night.boat.cairo'])[0]
    call_async(q, subscribe.Group, 'AddMembers', [handle], '')

    # libpurple puts him on our blist as soon as we've asked; there doesn't
    # seem to be any concept of remote-pending state.
    #
    # It also puts him in the default group, probably "Buddies".
    set_iq, _, _, _, new_channels = q.expect_many(
            EventPattern('stream-iq', iq_type='set',
                query_ns=ns.ROSTER, query_name='query'),
            EventPattern('stream-presence', presence_type='subscribe',
                to='suggs@night.boat.cairo'),
            EventPattern('dbus-return', method='AddMembers', value=()),
            # FIXME: TpBaseContactList wrongly assumes that he's the actor,
            # because he must have accepted our request... right? Wrong.
            EventPattern('dbus-signal', signal='MembersChanged',
                path=subscribe.object_path,
                args=['', [handle], [], [], [], handle, 0]),
            EventPattern('dbus-signal', signal='NewChannels',
                predicate=lambda e:
                    e.args[0][0][1].get(cs.TARGET_HANDLE_TYPE) == cs.HT_GROUP),
            )

    assertEquals('suggs@night.boat.cairo', set_iq.query.item['jid'])
    acknowledge_iq(stream, set_iq.stanza)

    # Suggs accepts our subscription request
    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = 'suggs@night.boat.cairo'
    presence['type'] = 'subscribed'
    stream.send(presence)

    # ... but nothing much happens, because there's no concept of pending
    # state in libpurple

    def_group = wrap_channel(bus.get_object(conn.bus_name,
        new_channels.args[0][0][0]), cs.CHANNEL_TYPE_CONTACT_LIST)
    handles = set(subscribe.Group.GetMembers())
    assertEquals(set([handle]), handles)

    # put a contact into the *group* explicitly: this shouldn't ask for
    # subscription, but it does
    handle = conn.RequestHandles(cs.HT_CONTACT, ['ayria@revenge.world'])[0]
    call_async(q, def_group.Group, 'AddMembers', [handle], '')

    # libpurple puts her on our blist as soon as we've asked; there doesn't
    # seem to be any concept of remote-pending state. It also puts her in the
    # same group.
    set_iq, _, _, _, _ = q.expect_many(
            EventPattern('stream-iq', iq_type='set',
                query_ns=ns.ROSTER, query_name='query'),
            EventPattern('stream-presence', presence_type='subscribe',
                to='ayria@revenge.world'),
            EventPattern('dbus-return', method='AddMembers', value=()),
            EventPattern('dbus-signal', signal='MembersChanged',
                path=subscribe.object_path,
                args=['', [handle], [], [], [], handle, 0]),
            EventPattern('dbus-signal', signal='MembersChanged',
                path=def_group.object_path,
                args=['', [handle], [], [], [], self_handle, 0]),
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
