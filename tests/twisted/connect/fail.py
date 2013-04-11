
"""
Test network error handling.
"""

import dbus

from servicetest import (assertEquals, assertContains)
from hazetest import exec_test
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])

    e = q.expect('dbus-signal', signal='ConnectionError')
    error, details = e.args

    assertEquals(cs.CONNECTION_FAILED, error)
    assertContains('debug-message', details)

    q.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_DISCONNECTED, cs.CSR_NETWORK_ERROR])

if __name__ == '__main__':
    exec_test(test, {'port': dbus.UInt32(14243)}, do_connect=False)

