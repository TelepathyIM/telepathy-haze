"""
Test text channel initiated by me, using Requests.
"""

import dbus

from twisted.words.xish import domish

from hazetest import exec_test
from servicetest import call_async, EventPattern

import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    self_handle = conn.GetSelfHandle()

    jid = 'foo@bar.com'
    call_async(q, conn, 'RequestHandles', 1, [jid])

    event = q.expect('dbus-return', method='RequestHandles')
    foo_handle = event.value[0][0]

    properties = conn.GetAll(
            'im.telepathy1.Connection.Interface.Requests',
            dbus_interface=dbus.PROPERTIES_IFACE)
    # Difference from Gabble: Haze's roster channels spring to life even if you
    # haven't received the XMPP roster.
    text_channels = [c for c in properties.get('Channels')
                if c[1][cs.CHANNEL_TYPE] == cs.CHANNEL_TYPE_TEXT
               ]
    assert text_channels == [], text_channels
    assert ({'im.telepathy1.Channel.ChannelType':
                'im.telepathy1.Channel.Type.Text',
             'im.telepathy1.Channel.TargetHandleType': 1,
             },
             ['im.telepathy1.Channel.TargetHandle',
              'im.telepathy1.Channel.TargetID'
             ],
             ) in properties.get('RequestableChannelClasses'),\
                     properties['RequestableChannelClasses']

    call_async(q, conn.Requests, 'CreateChannel',
            { 'im.telepathy1.Channel.ChannelType':
                'im.telepathy1.Channel.Type.Text',
              'im.telepathy1.Channel.TargetHandleType': 1,
              'im.telepathy1.Channel.TargetHandle': foo_handle,
              })

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    assert len(ret.value) == 2
    emitted_props = ret.value[1]
    assert emitted_props['im.telepathy1.Channel.ChannelType'] ==\
            'im.telepathy1.Channel.Type.Text'
    assert emitted_props['im.telepathy1.Channel.'
            'TargetHandleType'] == 1
    assert emitted_props['im.telepathy1.Channel.TargetHandle'] ==\
            foo_handle
    assert emitted_props['im.telepathy1.Channel.TargetID'] == jid
    assert emitted_props['im.telepathy1.Channel.'
            'Requested'] == True
    assert emitted_props['im.telepathy1.Channel.'
            'InitiatorHandle'] == self_handle
    assert emitted_props['im.telepathy1.Channel.'
            'InitiatorID'] == 'test@localhost'

    assert old_sig.args[0] == ret.value[0]
    assert old_sig.args[1] == u'org.freedesktop.Telepathy.Channel.Type.Text'
    # check that handle type == contact handle
    assert old_sig.args[2] == 1
    assert old_sig.args[3] == foo_handle
    assert old_sig.args[4] == True      # suppress handler

    assert len(new_sig.args) == 1
    assert len(new_sig.args[0]) == 1        # one channel
    assert len(new_sig.args[0][0]) == 2     # two struct members
    assert new_sig.args[0][0][0] == ret.value[0]
    assert new_sig.args[0][0][1] == ret.value[1]

    properties = conn.GetAll(
            'im.telepathy1.Connection.Interface.Requests',
            dbus_interface=dbus.PROPERTIES_IFACE)

    assert new_sig.args[0][0] in properties['Channels'], \
            (new_sig.args[0][0], properties['Channels'])

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

