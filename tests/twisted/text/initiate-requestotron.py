"""
Test text channel initiated by me, using Requests.
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

    jid = 'foo@bar.com'
    foo_handle = conn.get_contact_handle_sync(jid)

    properties = conn.GetAll(cs.CONN_IFACE_REQUESTS,
            dbus_interface=dbus.PROPERTIES_IFACE)
    # Difference from Gabble: Haze's roster channels spring to life even if you
    # haven't received the XMPP roster.
    text_channels = [c for c in properties.get('Channels')
                if c[1][cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_TEXT
               ]
    assert text_channels == [], text_channels
    assert ({cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
             cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
             },
             [cs.TARGET_HANDLE, cs.TARGET_ID],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    call_async(q, conn.Requests, 'CreateChannel',
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: foo_handle,
            })

    ret, sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    assert len(ret.value) == 2
    emitted_props = ret.value[1]
    assertEquals(cs.CHANNEL_TYPE_TEXT, emitted_props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_CONTACT, emitted_props[cs.TARGET_HANDLE_TYPE])
    assertEquals(foo_handle, emitted_props[cs.TARGET_HANDLE])
    assertEquals(jid, emitted_props[cs.TARGET_ID])
    assertEquals(True, emitted_props[cs.REQUESTED])
    assertEquals(self_handle, emitted_props[cs.INITIATOR_HANDLE])
    assertEquals('test@localhost', emitted_props[cs.INITIATOR_ID])

    assertLength(1, sig.args)
    assertLength(1, sig.args[0])        # one channel
    assertLength(2, sig.args[0][0])     # two struct members
    assertEquals(ret.value[0], sig.args[0][0][0])
    assertEquals(ret.value[1], sig.args[0][0][1])

    properties = conn.GetAll(cs.CONN_IFACE_REQUESTS,
            dbus_interface=dbus.PROPERTIES_IFACE)

    assertContains(sig.args[0][0], properties['Channels'])

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

