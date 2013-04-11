
"""
Test connecting to a server.
"""

from hazetest import exec_test
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])
    q.expect('stream-authenticated')

    # FIXME: unlike Gabble, Haze does not signal a presence update to
    # available during connect
    #q.expect('dbus-signal', signal='PresencesChanged',
    #    args=[{1L: (cs.PRESENCE_AVAILABLE, 'available', '')}])

    q.expect('dbus-signal', signal='StatusChanged', args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

if __name__ == '__main__':
    exec_test(test, do_connect=False)

