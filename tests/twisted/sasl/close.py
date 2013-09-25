"""Test the SASL channel being undispatchable."""

import dbus

from servicetest import EventPattern
from hazetest import exec_test, assertEquals
import constants as cs
from saslutil import connect_and_get_sasl_channel

JID = 'weaver@crobuzon.fic'

def test_no_password(q, bus, conn, stream):
    chan, props = connect_and_get_sasl_channel(q, bus, conn)

    chan.Close()

    _, _, status_changed = q.expect_many(
            EventPattern('dbus-signal', path=chan.object_path,
                signal='Closed'),
            EventPattern('dbus-signal', path=conn.object_path,
                signal='ChannelClosed', args=[chan.object_path]),
            # Unhelpfully prpl-jabber just sets the account to disabled so we
            # don't get an error.
            # EventPattern('dbus-signal', path=conn.object_path,
            #     signal='ConnectionError',
            #     predicate=lambda e: e.args[0] == cs.AUTHENTICATION_FAILED),
            EventPattern('dbus-signal', path=conn.object_path,
                signal='StatusChanged'),
            )

    status, reason = status_changed.args
    assertEquals(cs.CONN_STATUS_DISCONNECTED, status)
    # We would like to have
    # assertEquals(cs.CSR_AUTHENTICATION_FAILED, reason)

    # prpl-sipe does actually report a connection error rather
    # than just disabling the account, so yay. prpl-silc sets
    # PURPLE_CONNECTION_ERROR_OTHER_ERROR, which also comes out as
    # AUTHENTICATION_FAILED. No other prpls use
    # purple_account_request_password().

if __name__ == '__main__':
    exec_test(test_no_password, {'password': None,'account' : JID}, do_connect=False)
