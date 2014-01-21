
"""
Test text channel.
"""

import dbus

from twisted.words.xish import domish

from hazetest import exec_test
from servicetest import EventPattern, assertEquals, assertContains
import constants as cs

def test(q, bus, conn, stream):
    jid = 'foo@bar.com'

    # <message type="chat"><body>hello</body</message>
    m = domish.Element((None, 'message'))
    m['from'] = 'foo@bar.com/Pidgin'
    m['type'] = 'chat'
    m.addElement('body', content='hello')
    stream.send(m)

    event = q.expect('dbus-signal', signal='NewChannel')
    assertEquals(cs.CHANNEL_TYPE_TEXT, event.args[1][cs.CHANNEL_TYPE])
    assertEquals(cs.HT_CONTACT, event.args[1][cs.TARGET_HANDLE_TYPE])
    assertEquals(jid, event.args[1][cs.TARGET_ID])
    foo_at_bar_dot_com_handle = event.args[1][cs.TARGET_HANDLE]

    text_chan = bus.get_object(conn.bus_name, event.args[0])

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = text_chan.GetAll(cs.CHANNEL,
            dbus_interface=dbus.PROPERTIES_IFACE)
    assertEquals(foo_at_bar_dot_com_handle, channel_props.get('TargetHandle'))
    assert channel_props.get('TargetHandleType') == 1,\
            channel_props.get('TargetHandleType')
    assertEquals(cs.CHANNEL_TYPE_TEXT, channel_props.get('ChannelType'))
    assertContains(cs.CHANNEL_IFACE_CHAT_STATE,
            channel_props.get('Interfaces', ()))
    assert channel_props['TargetID'] == jid,\
            (channel_props['TargetID'], jid)
    assert channel_props['Requested'] == False
    assertEquals(foo_at_bar_dot_com_handle, channel_props['InitiatorHandle'])
    assert channel_props['InitiatorID'] == jid,\
            (channel_props['InitiatorID'], jid)

    message_received = q.expect('dbus-signal', signal='MessageReceived')

    message = message_received.args[0]

    # message should have two parts: the header and one content part
    assert len(message) == 2, message
    header, body = message

    assert header['message-sender'] == foo_at_bar_dot_com_handle, header
    # the spec says that message-type "MAY be omitted for normal chat
    # messages."
    assert 'message-type' not in header or header['message-type'] == 0, header

    assert body['content-type'] == 'text/plain', body
    assert body['content'] == 'hello', body

    # Remove the message from the pending message queue, and check that
    # PendingMessagesRemoved fires.
    message_id = header['pending-message-id']

    dbus.Interface(text_chan, cs.CHANNEL_TYPE_TEXT
        ).AcknowledgePendingMessages([message_id])

    removed = q.expect('dbus-signal', signal='PendingMessagesRemoved')

    removed_ids = removed.args[0]
    assert len(removed_ids) == 1, removed_ids
    assert removed_ids[0] == message_id, (removed_ids, message_id)

    # Send an action using the Messages API
    # In Gabble, this is a Notice, but we don't support those.
    greeting = [
        dbus.Dictionary({ 'message-type': cs.MT_ACTION,
                        }, signature='sv'),
        { 'content-type': 'text/plain',
          'content': u"waves",
        }
    ]

    dbus.Interface(text_chan, cs.CHANNEL_TYPE_TEXT
        ).SendMessage(greeting, dbus.UInt32(0))

    stream_message, message_sent = q.expect_many(
        EventPattern('stream-message'),
        EventPattern('dbus-signal', signal='MessageSent'),
        )

    elem = stream_message.stanza
    assert elem.name == 'message'
    assert elem['type'] == 'chat'

    found = False
    for e in elem.elements():
        if e.name == 'body':
            found = True
            e.children[0] == u'/me waves'
            break
    assert found, elem.toXml()

    sent_message = message_sent.args[0]
    assert len(sent_message) == 2, sent_message
    header = sent_message[0]
    assert header['message-type'] == 1, header # Action
    body = sent_message[1]
    assert body['content-type'] == 'text/plain', body
    assert body['content'] == u'waves', body

    # Send a message using Channel.Type.Text API
    dbus.Interface(text_chan, cs.CHANNEL_TYPE_TEXT
            ).SendMessage([{}, {
        'content-type': 'text/plain',
        'content': 'goodbye',
        }], 0)

    stream_message, message_sent = q.expect_many(
        EventPattern('stream-message'),
        EventPattern('dbus-signal', signal='MessageSent'),
        )

    elem = stream_message.stanza
    assert elem.name == 'message'
    assert elem['type'] == 'chat'

    found = False
    for e in elem.elements():
        if e.name == 'body':
            found = True
            e.children[0] == u'goodbye'
            break
    assert found, elem.toXml()

    sent_message = message_sent.args[0]
    assert len(sent_message) == 2, sent_message
    header = sent_message[0]
    # the spec says that message-type "MAY be omitted for normal chat
    # messages."
    assert 'message-type' not in header or header['message-type'] == 0, header
    body = sent_message[1]
    assert body['content-type'] == 'text/plain', body
    assert body['content'] == u'goodbye', body

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

