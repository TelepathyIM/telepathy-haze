"""
Test text channel being recreated because there are still pending messages.
"""

import dbus

from twisted.words.xish import domish

from hazetest import exec_test
from servicetest import call_async, EventPattern, assertEquals

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    self_handle = conn.GetSelfHandle()

    jid = 'foo@bar.com'
    call_async(q, conn, 'RequestHandles', 1, [jid])

    event = q.expect('dbus-return', method='RequestHandles')
    foo_handle = event.value[0][0]

    call_async(q, conn, 'RequestChannel',
        'org.freedesktop.Telepathy.Channel.Type.Text', 1, foo_handle, True)

    ret, old_sig, new_sig = q.expect_many(
        EventPattern('dbus-return', method='RequestChannel'),
        EventPattern('dbus-signal', signal='NewChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    text_chan = bus.get_object(conn.bus_name, ret.value[0])
    chan_iface = dbus.Interface(text_chan,
            'im.telepathy1.Channel')
    text_iface = dbus.Interface(text_chan,
            'org.freedesktop.Telepathy.Channel.Type.Text')

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

    # close the channel without acking the message; it comes back

    call_async(q, chan_iface, 'Close')

    old, new = q.expect_many(
            EventPattern('dbus-signal', signal='Closed'),
            EventPattern('dbus-signal', signal='ChannelClosed'),
            )
    assertEquals(text_chan.object_path, old.path)
    assertEquals(text_chan.object_path, new.args[0])

    event = q.expect('dbus-signal', signal='NewChannel')
    assert event.args[0] == text_chan.object_path
    assert event.args[1] == u'org.freedesktop.Telepathy.Channel.Type.Text'
    assert event.args[2] == 1   # CONTACT
    assert event.args[3] == foo_handle
    assert event.args[4] == False   # suppress handler

    event = q.expect('dbus-return', method='Close')

    # it now behaves as if the message had initiated it

    channel_props = text_chan.GetAll(
            'im.telepathy1.Channel',
            dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props['TargetID'] == jid,\
            (channel_props['TargetID'], jid)
    assert channel_props['Requested'] == False
    assert channel_props['InitiatorHandle'] == foo_handle,\
            (channel_props['InitiatorHandle'], foo_handle)
    assert channel_props['InitiatorID'] == 'foo@bar.com',\
            channel_props['InitiatorID']

    # the message is still there

    header['rescued'] = True
    messages = text_chan.Get(cs.CHANNEL_TYPE_TEXT, 'PendingMessages',
                            dbus_interface=cs.PROPERTIES_IFACE)
    assert messages == [[header, body]], messages

    # acknowledge it

    text_chan.AcknowledgePendingMessages([hello_message_id],
            dbus_interface='im.telepathy1.Channel.Type.Text')

    messages = text_chan.Get(cs.CHANNEL_TYPE_TEXT, 'PendingMessages',
                            dbus_interface=cs.PROPERTIES_IFACE)
    assert messages == []

    # close the channel again

    call_async(q, chan_iface, 'Close')

    event = q.expect('dbus-signal', signal='Closed')
    assertEquals(text_chan.object_path, event.path)

    event = q.expect('dbus-return', method='Close')

    # assert that it stays dead this time!

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

