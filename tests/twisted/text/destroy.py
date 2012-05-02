"""
Test text channel not being recreated because although there were still
pending messages, we destroyed it with extreme prejudice.
"""

import dbus

from twisted.words.xish import domish

from hazetest import exec_test
from servicetest import call_async, EventPattern, assertEquals
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    self_handle = conn.Properties.Get(cs.CONN, 'SelfHandle')

    jid = 'foo@bar.com'
    call_async(q, conn, 'RequestHandles', 1, [jid])

    event = q.expect('dbus-return', method='RequestHandles')
    foo_handle = event.value[0][0]

    call_async(q, conn.Requests, 'CreateChannel',
               {
            cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
            cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
            cs.TARGET_HANDLE: foo_handle
            })

    ret, new_sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    text_chan = bus.get_object(conn.bus_name, ret.value[0])
    chan_iface = dbus.Interface(text_chan,
            'im.telepathy1.Channel')
    text_iface = dbus.Interface(text_chan,
            'im.telepathy1.Channel.Type.Text')
    destroyable_iface = dbus.Interface(text_chan,
            'im.telepathy1.Channel.Interface.Destroyable1')

    assert len(new_sig.args) == 1
    assert len(new_sig.args[0]) == 1        # one channel
    assert len(new_sig.args[0][0]) == 2     # two struct members
    assert new_sig.args[0][0][0] == ret.value[0]
    emitted_props = new_sig.args[0][0][1]
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

    channel_props = text_chan.GetAll(
            'im.telepathy1.Channel',
            dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props['TargetID'] == jid,\
            (channel_props['TargetID'], jid)
    assert channel_props['Requested'] == True
    assert channel_props['InitiatorHandle'] == self_handle,\
            (channel_props['InitiatorHandle'], self_handle)
    assert channel_props['InitiatorID'] == 'test@localhost',\
            channel_props['InitiatorID']

    hey = [
        dbus.Dictionary({ 'message-type': cs.MT_NORMAL,
                        }, signature='sv'),
        { 'content-type': 'text/plain',
          'content': u"hey",
        }
    ]
    text_iface.SendMessage(hey, 0)

    event = q.expect('stream-message')

    elem = event.stanza
    assert elem.name == 'message'
    assert elem['type'] == 'chat'

    found = False
    for e in elem.elements():
        if e.name == 'body':
            found = True
            e.children[0] == u'hey'
            break
    assert found, elem.toXml()

    # <message type="chat"><body>hello</body</message>
    m = domish.Element((None, 'message'))
    m['from'] = 'foo@bar.com/Pidgin'
    m['type'] = 'chat'
    m.addElement('body', content='hello')
    stream.send(m)

    event = q.expect('dbus-signal', signal='MessageReceived')

    assertEquals(2, len(event.args[0]))
    header, body = event.args[0]
    hello_message_id = header['pending-message-id']
    hello_message_time = header['message-received'],
    assert header['message-sender'] == foo_handle
    # message type: normal
    assert header['message-type'] == cs.MT_NORMAL
    # body
    assert body['content'] == 'hello'

    messages = text_chan.Get(cs.CHANNEL_TYPE_TEXT, 'PendingMessages',
                            dbus_interface=cs.PROPERTIES_IFACE)
    assert messages == [[header, body]], messages

    # destroy the channel without acking the message; it does not come back

    call_async(q, destroyable_iface, 'Destroy')

    event = q.expect('dbus-signal', signal='Closed')
    assertEquals(text_chan.object_path, event.path)

    event = q.expect('dbus-return', method='Destroy')

    # assert that it stays dead

    try:
        chan_iface.GetChannelType()
    except dbus.DBusException:
        pass
    else:
        raise AssertionError("Why won't it die?")

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

