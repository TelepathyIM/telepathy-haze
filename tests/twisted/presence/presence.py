"""
A simple smoke-test for C.I.SimplePresence
"""

import dbus

from twisted.words.xish import domish, xpath
from twisted.words.protocols.jabber.client import IQ

from servicetest import assertEquals
from hazetest import exec_test
import constants as cs

def test(q, bus, conn, stream):
    amy_handle = conn.RequestHandles(1, ['amy@foo.com'])[0]

    # Divergence from Gabble: hazetest responds to all roster gets with an
    # empty roster, so we need to push the roster.
    iq = IQ(stream, 'set')
    query = iq.addElement(('jabber:iq:roster', 'query'))
    item = query.addElement('item')
    item['jid'] = 'amy@foo.com'
    item['subscription'] = 'both'

    stream.send(iq)

    presence = domish.Element((None, 'presence'))
    presence['from'] = 'amy@foo.com'
    show = presence.addElement((None, 'show'))
    show.addContent('away')
    status = presence.addElement((None, 'status'))
    status.addContent('At the pub')
    stream.send(presence)

    event = q.expect('dbus-signal', signal='PresencesChanged')
    assert event.args[0] == { amy_handle: (3, 'away', 'At the pub') }

    presence = domish.Element((None, 'presence'))
    presence['from'] = 'amy@foo.com'
    show = presence.addElement((None, 'show'))
    show.addContent('chat')
    status = presence.addElement((None, 'status'))
    status.addContent('I may have been drinking')
    stream.send(presence)

    event = q.expect('dbus-signal', signal='PresencesChanged')
    # FIXME: 'chat' gets lost somewhere between the XMPP stream and what Haze
    # produces.
    assert event.args[0] == { amy_handle: (2, 'available', 'I may have been drinking') }

    amy_handle, asv = conn.Contacts.GetContactByID('amy@foo.com',
            [cs.CONN_IFACE_SIMPLE_PRESENCE])
    assertEquals(event.args[0][amy_handle], asv.get(cs.ATTR_PRESENCE))

    bob_handle, asv = conn.Contacts.GetContactByID('bob@foo.com',
            [cs.CONN_IFACE_SIMPLE_PRESENCE])
    assertEquals((cs.PRESENCE_UNKNOWN, 'unknown', ''),
            asv.get(cs.ATTR_PRESENCE))

    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

