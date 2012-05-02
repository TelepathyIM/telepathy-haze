
"""
Test that <message>s with a chat state notification but no body don't create a
new text channel.
"""

from twisted.words.xish import domish

from servicetest import assertEquals
from hazetest import exec_test
import constants as cs

import ns

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    # message without body
    m = domish.Element((None, 'message'))
    m['from'] = 'alice@foo.com'
    m['type'] = 'chat'
    m.addElement((ns.CHAT_STATES, 'composing'))
    stream.send(m)

    # message with body
    m = domish.Element((None, 'message'))
    m['from'] = 'bob@foo.com'
    m['type'] = 'chat'
    m.addElement((ns.CHAT_STATES, 'active'))
    m.addElement('body', content='hello')
    stream.send(m)

    # first message should be from Bob, not Alice
    event = q.expect('dbus-signal', signal='NewChannels')
    assertEquals(1, len(event.args[0]))
    path, props = event.args[0][0]
    assertEquals(cs.CHANNEL_TYPE_TEXT, props[cs.CHANNEL_TYPE])
    assertEquals('bob@foo.com', props[cs.TARGET_ID])
    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

