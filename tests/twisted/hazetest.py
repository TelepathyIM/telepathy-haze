"""
Infrastructure code for testing Haze by pretending to be a Jabber server.
"""

import sys
from twisted.words.xish import domish, xpath
import dbus

import servicetest
# reexport everything, but override a thing
from gabbletest import *

class EmptyRosterXmppXmlStream(XmppXmlStream):
    def _cb_authd(self, x):
        XmppXmlStream._cb_authd(self, x)

        self.addObserver(
            "/iq/query[@xmlns='jabber:iq:roster']",
            self._cb_roster_get)

    def _cb_roster_get(self, iq):
        # Just send back an empty roster. prpl-jabber waits for the roster
        # before saying it's online.
        if iq.getAttribute('type') == 'get':
            self.send(make_result_iq(self, iq))

def make_haze_connection(bus, event_func, params=None, suffix=''):
    # Gabble accepts a resource in 'account', but the value of 'resource'
    # overrides it if there is one. Haze doesn't.
    # account = 'test%s@localhost/%s' % (suffix, re.sub(r'.*/', '', sys.argv[0]))
    account = 'test%s@localhost/Resource' % (suffix, )

    default_params = {
        'account': account,
        'password': 'pass',
        'ft-proxies': sys.argv[0],
        'server': 'localhost',
        'port': dbus.UInt32(4242),
        'require-encryption': False,
        'auth-plain-in-clear': True,
        }

    if params:
        default_params.update(params)

     # Allow omitting the 'password' param
    if default_params['password'] is None:
        del default_params['password']

     # Allow omitting the 'account' param
    if default_params['account'] is None:
        del default_params['account']

    jid = default_params.get('account', None)
    conn =  servicetest.make_connection(bus, event_func, 'haze', 'jabber',
                                        default_params)
    return (conn, jid)

def expect_kinda_connected(queue):
    queue.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTING, cs.CSR_REQUESTED])
    queue.expect('stream-authenticated')
    # FIXME: unlike Gabble, Haze does not signal a presence update to available
    # during connect
    # queue.expect('dbus-signal', signal='PresencesChanged',
    #     args=[{1L: (cs.PRESENCE_AVAILABLE, u'available', '')}])
    queue.expect('dbus-signal', signal='StatusChanged',
        args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

# Copy pasta because we need to replace make_connection
def exec_test(fun, params=None, protocol=EmptyRosterXmppXmlStream, timeout=None,
              authenticator=None, num_instances=1, do_connect=True):
    reactor.callWhenRunning(
        exec_test_deferred, fun, params, protocol, timeout, authenticator, num_instances,
        do_connect, make_haze_connection, expect_kinda_connected)
    reactor.run()

def close_all_groups(q, bus, conn, stream):
    channels = conn.Properties.Get(cs.CONN_IFACE_REQUESTS, 'Channels')
    for path, props in channels:
        if props.get(cs.CHANNEL_TYPE) != cs.CHANNEL_TYPE_CONTACT_LIST:
            continue
        if props.get(cs.TARGET_HANDLE_TYPE) != cs.HT_GROUP:
            continue
        wrap_channel(bus.get_object(conn.bus_name, path),
                cs.CHANNEL_TYPE_CONTACT_LIST).Close()

