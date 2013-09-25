"""
Test text channel not being recreated because although there were still
pending messages, we destroyed it with extreme prejudice.
"""

import dbus

from twisted.words.xish import domish

from hazetest import exec_test
from servicetest import (call_async, EventPattern, assertEquals, assertLength,
        assertContains)
import constants as cs

def test(q, bus, conn, stream):
    self_handle = conn.Properties.Get(cs.CONN, "SelfHandle")

    jid = 'foo@bar.com'
    foo_handle = conn.get_contact_handle_sync(jid)

    call_async(q, conn.Requests, 'CreateChannel',
            { cs.CHANNEL_TYPE: cs.CHANNEL_TYPE_TEXT,
              cs.TARGET_HANDLE_TYPE: cs.HT_CONTACT,
              cs.TARGET_HANDLE: foo_handle,
            })

    ret, sig = q.expect_many(
        EventPattern('dbus-return', method='CreateChannel'),
        EventPattern('dbus-signal', signal='NewChannels'),
        )

    text_chan = bus.get_object(conn.bus_name, ret.value[0])
    chan_iface = dbus.Interface(text_chan, cs.CHANNEL)
    text_iface = dbus.Interface(text_chan, cs.CHANNEL_TYPE_TEXT)
    messages_iface = dbus.Interface(text_chan, cs.CHANNEL_IFACE_MESSAGES)
    destroyable_iface = dbus.Interface(text_chan, cs.CHANNEL_IFACE_DESTROYABLE)

    assertLength(1, sig.args)
    assertLength(1, sig.args[0])        # one channel
    assertLength(2, sig.args[0][0])     # two struct members
    assertEquals(ret.value, sig.args[0][0])
    emitted_props = sig.args[0][0][1]
    assertEquals(cs.CHANNEL_TYPE_TEXT, emitted_props[cs.CHANNEL_TYPE])
    assertEquals(cs.HT_CONTACT, emitted_props[cs.TARGET_HANDLE_TYPE])
    assertEquals(foo_handle, emitted_props[cs.TARGET_HANDLE])
    assertEquals(jid, emitted_props[cs.TARGET_ID])
    assertEquals(True, emitted_props[cs.REQUESTED])
    assertEquals(self_handle, emitted_props[cs.INITIATOR_HANDLE])
    assertEquals('test@localhost', emitted_props[cs.INITIATOR_ID])
    assertContains(cs.CHANNEL_IFACE_MESSAGES, emitted_props[cs.INTERFACES])
    assertContains(cs.CHANNEL_IFACE_DESTROYABLE, emitted_props[cs.INTERFACES])

    channel_props = text_chan.GetAll(cs.CHANNEL,
            dbus_interface=dbus.PROPERTIES_IFACE)
    assert channel_props['TargetID'] == jid,\
            (channel_props['TargetID'], jid)
    assert channel_props['Requested'] == True
    assert channel_props['InitiatorHandle'] == self_handle,\
            (channel_props['InitiatorHandle'], self_handle)
    assert channel_props['InitiatorID'] == 'test@localhost',\
            channel_props['InitiatorID']

    messages_iface.SendMessage([{}, {
        'content-type': 'text/plain',
        'content': 'hey',
        }], 0)

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

    message = event.args[0]
    assertLength(2, message)
    hello_message_id = message[0]['pending-message-id']
    assertEquals(foo_handle, message[0]['message-sender'])
    assertEquals('foo@bar.com', message[0]['message-sender-id'])
    assertEquals(cs.MT_NORMAL,
            message[0].get('message-type', cs.MT_NORMAL))
    assertEquals('text/plain', message[1]['content-type'])
    assertEquals('hello', message[1]['content'])

    messages = text_chan.Get(cs.CHANNEL_IFACE_MESSAGES, 'PendingMessages',
            dbus_interface=cs.PROPERTIES_IFACE)
    assertEquals([message], messages)

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

