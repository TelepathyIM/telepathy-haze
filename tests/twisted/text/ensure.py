"""
Test text channel initiated by me, using Requests.EnsureChannel
"""

import dbus

from twisted.words.xish import domish

from hazetest import exec_test
from servicetest import (call_async, EventPattern, unwrap, assertEquals,
        assertLength, assertContains)

import pprint

import constants as cs

def test(q, bus, conn, stream):
    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")

    jids = ['foo@bar.com', 'truc@cafe.fr']
    handles = conn.get_contact_handles_sync(jids)

    properties = conn.GetAll(cs.CONN_IFACE_REQUESTS,
            dbus_interface=dbus.PROPERTIES_IFACE)
    # Difference from Gabble: Haze's roster channels spring to life even if you
    # haven't received the XMPP roster.
    text_channels = [c for c in properties.get('Channels')
                if c[1][cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_TEXT
               ]
    assert text_channels == [], text_channels

    properties = conn.GetAll(cs.CONN, dbus_interface=dbus.PROPERTIES_IFACE)
    assert ({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
             cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
             },
             [cs.TARGET_HANDLE, cs.TARGET_ID],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    test_ensure_ensure(q, conn, self_handle, jids[0], handles[0])
    test_request_ensure(q, conn, self_handle, jids[1], handles[1])

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])


def test_ensure_ensure(q, conn, self_handle, jid, handle):
    """
    Test ensuring a non-existant channel twice.  The first call should succeed
    with Yours=True; the subsequent call should succeed with Yours=False
    """

    # Check that Ensuring a channel that doesn't exist succeeds
    call_async(q, conn.Requests, 'EnsureChannel', request_props (handle))

    ret, sig = q.expect_many(
        EventPattern('dbus-return', method='EnsureChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        )

    assert len(ret.value) == 3
    yours, path, emitted_props = ret.value

    # The channel was created in response to the call, and we were the only
    # requestor, so we should get Yours=True
    assert yours, ret.value

    check_props(emitted_props, self_handle, handle, jid)

    assertEquals(path, sig.args[0])
    assertEquals(emitted_props, sig.args[1])

    properties = conn.GetAll(cs.CONN_IFACE_REQUESTS,
            dbus_interface=dbus.PROPERTIES_IFACE)

    assertContains((sig.args[0], sig.args[1]), properties['Channels'])

    # Now try Ensuring a channel which already exists
    call_async(q, conn.Requests, 'EnsureChannel', request_props (handle))
    ret_ = q.expect('dbus-return', method='EnsureChannel')

    assert len(ret_.value) == 3
    yours_, path_, emitted_props_ = ret_.value

    # Someone's already responsible for this channel, so we should get
    # Yours=False
    assert not yours_, ret_.value
    assert path == path_, (path, path_)
    assert emitted_props == emitted_props_, (emitted_props, emitted_props_)


def test_request_ensure(q, conn, self_handle, jid, handle):
    """
    Test Creating a non-existant channel, then Ensuring the same channel.
    The call to Ensure should succeed with Yours=False.
    """

    call_async(q, conn.Requests, 'CreateChannel', request_props (handle))

    ret, sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        )

    assert len(ret.value) == 2
    path, emitted_props = ret.value

    check_props(emitted_props, self_handle, handle, jid)

    assertEquals(path, sig.args[0])
    assertEquals(emitted_props, sig.args[1])

    properties = conn.GetAll(cs.CONN_IFACE_REQUESTS,
            dbus_interface=dbus.PROPERTIES_IFACE)

    assertContains((sig.args[0], sig.args[1]), properties['Channels'])

    # Now try Ensuring that same channel.
    call_async(q, conn.Requests, 'EnsureChannel', request_props (handle))
    ret_ = q.expect('dbus-return', method='EnsureChannel')

    assert len(ret_.value) == 3
    yours_, path_, emitted_props_ = ret_.value

    # Someone's already responsible for this channel, so we should get
    # Yours=False
    assert not yours_, ret_.value
    assert path == path_, (path, path_)
    assert emitted_props == emitted_props_, (emitted_props, emitted_props_)


def check_props(props, self_handle, handle, jid):
    assertEquals(cs.CHANNEL_TYPE_TEXT, props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_CONTACT, props[cs.TARGET_HANDLE_TYPE])
    assertEquals(handle, props[cs.TARGET_HANDLE])
    assertEquals(jid, props[cs.TARGET_ID])
    assertEquals(True, props[cs.REQUESTED])
    assertEquals(self_handle, props[cs.INITIATOR_HANDLE])
    assertEquals('test@localhost', props[cs.INITIATOR_ID])


def request_props(handle):
    return { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
             cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
             cs.TARGET_HANDLE: handle,
           }


if __name__ == '__main__':
    exec_test(test)

