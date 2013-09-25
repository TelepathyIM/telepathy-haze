
"""
Test receiving delayed (offline) messages on a text channel.
"""

import datetime

from twisted.words.xish import domish

from hazetest import exec_test
from servicetest import EventPattern, assertEquals
import constants as cs

def test(q, bus, conn, stream):
    m = domish.Element((None, 'message'))
    m['from'] = 'foo@bar.com'
    m['type'] = 'chat'
    m.addElement('body', content='hello')

    # add timestamp information
    x = m.addElement(('jabber:x:delay', 'x'))
    x['stamp'] = '20070517T16:15:01'

    stream.send(m)

    event = q.expect('dbus-signal', signal='NewChannels')
    assertEquals(cs.CHANNEL_TYPE_TEXT, event.args[0][0][1][cs.CHANNEL_TYPE])
    assertEquals(cs.HT_CONTACT, event.args[0][0][1][cs.TARGET_HANDLE_TYPE])
    assertEquals('foo@bar.com', event.args[0][0][1][cs.TARGET_ID])

    message_received = q.expect('dbus-signal', signal='MessageReceived')

    message = message_received.args[0]
    header = message[0]
    message_sent_timestamp = header['message-sent']
    new_signal_time = str(datetime.datetime.utcfromtimestamp(message_sent_timestamp))
    assert new_signal_time == '2007-05-17 16:15:01', (headers, new_signal_time)
    message_received_timestamp = header['message-received']
    assert message_received_timestamp > message_sent_timestamp, headers

    assert message[1]['content'] == 'hello', message


    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

