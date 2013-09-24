"""
Test requests to see our presence.
"""

import dbus

from twisted.words.protocols.jabber.client import IQ
from twisted.words.xish import domish

from servicetest import (EventPattern, wrap_channel, assertLength,
        assertEquals, call_async, sync_dbus, assertSameSets)
from hazetest import acknowledge_iq, exec_test, sync_stream
import constants as cs
import ns

# TODO: this needs porting to ContactList
raise SystemExit(77)

def test(q, bus, conn, stream):
    call_async(q, conn.ContactList, 'GetContactListAttributes',
            [cs.CONN_IFACE_CONTACT_GROUPS], False)
    r = q.expect('dbus-return', method='GetContactListAttributes')
    assertLength(0, r.value[0].keys())

    # receive a subscription request
    alice = conn.get_contact_handle_sync('alice@wonderland.lit')

    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = 'alice@wonderland.lit'
    presence['type'] = 'subscribe'
    presence.addElement('status', content='friend me')
    stream.send(presence)

    # it seems either libpurple or haze doesn't pass the message through
    q.expect_many(
            EventPattern('dbus-signal', signal='ContactsChangedWithID',
                args=[{
                    alice:
                        (cs.SUBSCRIPTION_STATE_NO, cs.SUBSCRIPTION_STATE_ASK,
                            ''),
                    },
                    {alice: 'alice@wonderland.lit'}, {}]),
            )

    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")

    # accept
    call_async(q, conn.ContactList, 'AuthorizePublication', [alice])

    q.expect_many(
            EventPattern('stream-presence', presence_type='subscribed',
                to='alice@wonderland.lit'),
            EventPattern('dbus-signal', signal='ContactsChangedWithID',
                args=[{
                    alice:
                        (cs.SUBSCRIPTION_STATE_NO, cs.SUBSCRIPTION_STATE_YES,
                            ''),
                    },
                    {alice: 'alice@wonderland.lit'}, {}]),
            EventPattern('dbus-return', method='AuthorizePublication'),
            )

    # the server sends us a roster push
    iq = IQ(stream, 'set')
    iq['id'] = 'roster-push'
    query = iq.addElement(('jabber:iq:roster', 'query'))
    item = query.addElement('item')
    item['jid'] = 'alice@wonderland.lit'
    item['subscription'] = 'from'

    stream.send(iq)

    q.expect_many(
            EventPattern('stream-iq', iq_type='result',
                predicate=lambda e: e.stanza['id'] == 'roster-push'),
            # She's not really on our subscribe list, but this is the closest
            # we can guess from libpurple
            EventPattern('dbus-signal', signal='ContactsChangedWithID',
                args=[{
                    alice:
                        (cs.SUBSCRIPTION_STATE_YES, cs.SUBSCRIPTION_STATE_YES,
                            ''),
                    },
                    {alice: 'alice@wonderland.lit'}, {}]),
            # the buddy needs a group, because libpurple
            EventPattern('dbus-signal', signal='GroupsChanged',
                predicate=lambda e: e.args[0] == [alice]),
            )

    call_async(q, conn.ContactList, 'GetContactListAttributes',
            [cs.CONN_IFACE_CONTACT_GROUPS], False)
    r = q.expect('dbus-return', method='GetContactListAttributes')
    assertSameSets([alice], r.value[0].keys())

    # receive another subscription request
    queen = conn.get_contact_handle_sync('queen.of.hearts@wonderland.lit')

    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = 'queen.of.hearts@wonderland.lit'
    presence['type'] = 'subscribe'
    presence.addElement('status', content='Off with her head!')
    stream.send(presence)

    # it seems either libpurple or haze doesn't pass the message through
    q.expect_many(
            EventPattern('dbus-signal', signal='ContactsChangedWithID',
                args=[{
                    queen:
                        (cs.SUBSCRIPTION_STATE_NO, cs.SUBSCRIPTION_STATE_ASK,
                            ''),
                    },
                    {queen: 'queen.of.hearts@wonderland.lit'}, {}]),
            )

    # the contact is temporarily on our roster
    call_async(q, conn.ContactList, 'GetContactListAttributes',
            [cs.CONN_IFACE_CONTACT_GROUPS], False)
    r = q.expect('dbus-return', method='GetContactListAttributes')
    assertSameSets([alice, queen], r.value[0].keys())

    # decline
    call_async(q, conn.ContactList, 'RemoveContacts', [queen])

    q.expect_many(
            EventPattern('stream-presence', presence_type='unsubscribed',
                to='queen.of.hearts@wonderland.lit'),
            EventPattern('dbus-signal', signal='ContactsChangedWithID',
                args=[{
                    queen:
                        (cs.SUBSCRIPTION_STATE_NO, cs.SUBSCRIPTION_STATE_NO,
                            ''),
                    }, {queen: 'queen.of.hearts@wonderland.lit'}, {}]),
            EventPattern('dbus-return', method='RemoveContacts'),
            )

    sync_dbus(bus, q, conn)
    sync_stream(q, stream)

    # the declined contact isn't on our roster any more
    call_async(q, conn.ContactList, 'GetContactListAttributes',
            [cs.CONN_IFACE_CONTACT_GROUPS], False)
    r = q.expect('dbus-return', method='GetContactListAttributes')
    assertSameSets([alice], r.value[0].keys())

    # she's persistent
    presence = domish.Element(('jabber:client', 'presence'))
    presence['from'] = 'queen.of.hearts@wonderland.lit'
    presence['type'] = 'subscribe'
    presence.addElement('status', content='How dare you?')
    stream.send(presence)

    q.expect_many(
            EventPattern('dbus-signal', signal='ContactsChangedWithID',
                args=[{
                    queen:
                        (cs.SUBSCRIPTION_STATE_NO, cs.SUBSCRIPTION_STATE_ASK,
                            ''),
                        },
                        {queen: 'queen.of.hearts@wonderland.lit'}, {}]),
            )

    # disconnect with the request outstanding, to make sure we don't crash
    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_REQUESTED])
    # make sure Haze didn't crash
    sync_dbus(bus, q, conn)

if __name__ == '__main__':
    exec_test(test)
