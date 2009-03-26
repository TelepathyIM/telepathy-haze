"""
Regression test for <https://bugs.freedesktop.org/show_bug.cgi?id=18361>:
requesting a connection for an account that's already connected crashed Haze
shortly afterwards.
"""

import dbus

from hazetest import exec_test
from servicetest import tp_name_prefix, tp_path_prefix
import constants as cs

def test(q, bus, conn, stream):
    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[1, 1])
    q.expect('stream-authenticated')

    # FIXME: unlike Gabble, Haze does not signal a presence update to
    # available during connect
    #q.expect('dbus-signal', signal='PresenceUpdate',
    #    args=[{1L: (0L, {u'available': {}})}])

    q.expect('dbus-signal', signal='StatusChanged', args=[0, 1])

    haze = bus.get_object(
        tp_name_prefix + '.ConnectionManager.haze',
        tp_path_prefix + '/ConnectionManager/haze')
    cm_iface = dbus.Interface(haze, cs.CM)

    params = {
        'account': 'test@localhost/Resource',
        'password': 'pass',
        'server': 'localhost',
        # FIXME: the spec says this is a UInt32 and Gabble agrees
        'port': dbus.Int32(4242),
        }

    # You might think that this is the test...
    try:
        cm_iface.RequestConnection('jabber', params)
    except dbus.DBusException, e:
        # tp-glib <0.7.28 got the error domain wrong! :D
        assert e.get_dbus_name().endswith("NotAvailable")

    # but you'd be wrong: we now test that Haze is still alive.
    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

