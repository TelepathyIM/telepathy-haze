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

    properties = conn.GetAll(
            'org.freedesktop.Telepathy.Connection.Interface.Requests',
            dbus_interface=dbus.PROPERTIES_IFACE)
    # Difference from Gabble: Haze's roster channels spring to life even if you
    # haven't received the XMPP roster.
    text_channels = [c for c in properties.get('Channels')
                if c[1][cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_TEXT
               ]
    assert text_channels == [], text_channels
    assert ({'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.Text',
             'org.freedesktop.Telepathy.Channel.TargetHandleType': 1,
             },
             ['org.freedesktop.Telepathy.Channel.TargetHandle',
              'org.freedesktop.Telepathy.Channel.TargetID'
             ],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    call_async(q, conn.Requests, 'CreateChannel',
            { 'org.freedesktop.Telepathy.Channel.ChannelType':
                'org.freedesktop.Telepathy.Channel.Type.Text',
              'org.freedesktop.Telepathy.Channel.TargetHandleType': 1,
              'org.freedesktop.Telepathy.Channel.TargetHandle': foo_handle,
              })

    ret, sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    assert len(ret.value) == 2
    emitted_props = ret.value[1]
    assert emitted_props['org.freedesktop.Telepathy.Channel.ChannelType'] ==\
            'org.freedesktop.Telepathy.Channel.Type.Text'
    assert emitted_props['org.freedesktop.Telepathy.Channel.'
            'TargetHandleType'] == 1
    assert emitted_props['org.freedesktop.Telepathy.Channel.TargetHandle'] ==\
            foo_handle
    assert emitted_props['org.freedesktop.Telepathy.Channel.TargetID'] == jid
    assert emitted_props['org.freedesktop.Telepathy.Channel.'
            'Requested'] == True
    assert emitted_props['org.freedesktop.Telepathy.Channel.'
            'InitiatorHandle'] == self_handle
    assert emitted_props['org.freedesktop.Telepathy.Channel.'
            'InitiatorID'] == 'test@localhost'

    assertLength(1, sig.args)
    assertLength(1, sig.args[0])        # one channel
    assertLength(2, sig.args[0][0])     # two struct members
    assertEquals(ret.value[0], sig.args[0][0][0])
    assertEquals(ret.value[1], sig.args[0][0][1])

    properties = conn.GetAll(
            'org.freedesktop.Telepathy.Connection.Interface.Requests',
            dbus_interface=dbus.PROPERTIES_IFACE)

    assertContains(sig.args[0][0], properties['Channels'])

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

