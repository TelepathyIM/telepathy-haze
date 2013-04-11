
"""
Test text channel.
"""

import dbus

from twisted.words.xish import domish

from hazetest import exec_test
from servicetest import EventPattern

def test(q, bus, conn, stream):
    # <message type="chat"><body>hello</body</message>
    m = domish.Element((None, 'message'))
    m['from'] = 'foo@bar.com/Pidgin'
    m['type'] = 'chat'
    m.addElement('body', content='hello')
    stream.send(m)

    event = q.expect('dbus-signal', signal='NewChannel')
    text_chan = bus.get_object(conn.bus_name, event.args[0])
    assert event.args[1] == u'org.freedesktop.Telepathy.Channel.Type.Text'
    # check that handle type == contact handle
    assert event.args[2] == 1
    foo_at_bar_dot_com_handle = event.args[3]
    jid = conn.InspectHandles(1, [foo_at_bar_dot_com_handle])[0]
    assert jid == 'foo@bar.com'
    assert event.args[4] == False   # suppress handler

    # Exercise basic Channel Properties from spec 0.17.7
    channel_props = text_chan.GetAll(
            'org.freedesktop.Telepathy.Channel',
            dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props.get('TargetHandle') == event.args[3],\
            (channel_props.get('TargetHandle'), event.args[3])
    assert channel_props.get('TargetHandleType') == 1,\
            channel_props.get('TargetHandleType')
    assert channel_props.get('ChannelType') == \
            'org.freedesktop.Telepathy.Channel.Type.Text',\
            channel_props.get('ChannelType')
    assert 'org.freedesktop.Telepathy.Channel.Interface.ChatState' in \
            channel_props.get('Interfaces', ()), \
            channel_props.get('Interfaces')
    assert 'org.freedesktop.Telepathy.Channel.Interface.Messages' in \
            channel_props.get('Interfaces', ()), \
            channel_props.get('Interfaces')
    assert channel_props['TargetID'] == jid,\
            (channel_props['TargetID'], jid)
    assert channel_props['Requested'] == False
    assert channel_props['InitiatorHandle'] == event.args[3],\
            (channel_props['InitiatorHandle'], event.args[3])
    assert channel_props['InitiatorID'] == jid,\
            (channel_props['InitiatorID'], jid)

    received, message_received = q.expect_many(
        EventPattern('dbus-signal', signal='Received'),
        EventPattern('dbus-signal', signal='MessageReceived'),
        )

    # Check that C.T.Text.Received looks right
    # message type: normal
    assert received.args[3] == 0
    # flags: none
    assert received.args[4] == 0
    # body
    assert received.args[5] == 'hello'


    # Check that C.I.Messages.MessageReceived looks right.
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

    dbus.Interface(text_chan,
        u'org.freedesktop.Telepathy.Channel.Type.Text'
        ).AcknowledgePendingMessages([message_id])

    removed = q.expect('dbus-signal', signal='PendingMessagesRemoved')

    removed_ids = removed.args[0]
    assert len(removed_ids) == 1, removed_ids
    assert removed_ids[0] == message_id, (removed_ids, message_id)

    # Send an action using the Messages API
    # In Gabble, this is a Notice, but we don't support those.
    greeting = [
        dbus.Dictionary({ 'message-type': 1, # Action
                        }, signature='sv'),
        { 'content-type': 'text/plain',
          'content': u"waves",
        }
    ]

    dbus.Interface(text_chan,
        u'org.freedesktop.Telepathy.Channel.Interface.Messages'
        ).SendMessage(greeting, dbus.UInt32(0))

    stream_message, sent, message_sent = q.expect_many(
        EventPattern('stream-message'),
        EventPattern('dbus-signal', signal='Sent'),
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

    assert sent.args[1] == 1, sent.args # Action
    assert sent.args[2] == u'waves', sent.args


    # Send a message using Channel.Type.Text API
    dbus.Interface(text_chan,
        u'org.freedesktop.Telepathy.Channel.Type.Text').Send(0, 'goodbye')

    stream_message, sent, message_sent = q.expect_many(
        EventPattern('stream-message'),
        EventPattern('dbus-signal', signal='Sent'),
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

    assert sent.args[1] == 0, sent.args # message type normal
    assert sent.args[2] == u'goodbye', sent.args


    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

