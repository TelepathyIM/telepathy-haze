"""
Test Protocol objects.
"""

import dbus

import constants as cs
from servicetest import (assertEquals, tp_path_prefix, assertContains,
        assertDoesNotContain, call_async)
from hazetest import exec_test

def test(q, bus, conn, stream):
    # Ignore conn here, we're only dealing with the CM and Protocol objects.
    cm = bus.get_object(cs.CM + '.haze',
        tp_path_prefix + '/ConnectionManager/haze')
    cm_iface = dbus.Interface(cm, cs.CM)
    cm_props = dbus.Interface(cm, cs.PROPERTIES_IFACE)

    protocols = cm_props.Get(cs.CM, 'Protocols')
    protocol_names = cm_iface.ListProtocols()
    assertEquals(set(protocols.iterkeys()), set(protocol_names))

    for name in protocol_names:
        props = protocols[name]
        protocol = bus.get_object(cm.bus_name,
            cm.object_path + '/' + name.replace('-', '_'))
        protocol_iface = dbus.Interface(protocol, cs.PROTOCOL)
        protocol_props = dbus.Interface(protocol, cs.PROPERTIES_IFACE)
        flat_props = protocol_props.GetAll(cs.PROTOCOL)

        parameters = cm_iface.GetParameters(name)
        assertEquals(parameters, props[cs.PROTOCOL + '.Parameters'])
        assertEquals(parameters, flat_props['Parameters'])
        assertEquals(parameters, protocol_props.Get(cs.PROTOCOL, 'Parameters'))

        assertEquals(flat_props['VCardField'],
                props[cs.PROTOCOL + '.VCardField'])
        assertEquals(flat_props['Interfaces'],
                props[cs.PROTOCOL + '.Interfaces'])
        assertEquals(flat_props['EnglishName'],
                props[cs.PROTOCOL + '.EnglishName'])
        assertEquals(flat_props['Icon'], props[cs.PROTOCOL + '.Icon'])
        assertEquals(flat_props['ConnectionInterfaces'],
            props[cs.PROTOCOL + '.ConnectionInterfaces'])
        assertEquals(flat_props['RequestableChannelClasses'],
            props[cs.PROTOCOL + '.RequestableChannelClasses'])

        param_map = {}
        param_flags = {}
        param_type = {}
        param_def = {}

        for p in parameters:
            param_map[p[0]] = tuple(p[1:])
            param_flags[p[0]] = p[1]
            param_type[p[0]] = p[2]
            param_def[p[0]] = p[3]

        # We use special cases to rename these; make sure they don't come back
        assertDoesNotContain(name, ('meanwhile', 'simple'))
        assertDoesNotContain('encoding', param_map)
        assertDoesNotContain('local_charset', param_map)

        if name not in ('local-xmpp', 'irc'):
            # it would be more correct for these protocols not to have this
            # parameter
            assertEquals((cs.PARAM_REQUIRED, 's', ''), param_map['account'])

        # a random selection of checks for known parameters...

        if name == 'gadugadu':
            assertEquals('x-gadugadu', flat_props['VCardField'])
            assertEquals('im-gadugadu', flat_props['Icon'])
            assertEquals((cs.PARAM_SECRET, 's', ''),
                    param_map['password'])
            assertEquals('s', param_type['nick'])
            assertEquals('s', param_type['gg-server'])
        elif name == 'silc':
            assertEquals('x-silc', flat_props['VCardField'])
            assertEquals('im-silc', flat_props['Icon'])
            assertEquals((cs.PARAM_SECRET, 's', ''), param_map['password'])
            assertEquals('s', param_type['server'])
        elif name == 'irc':
            assertEquals('x-irc', flat_props['VCardField'])
            assertEquals('im-irc', flat_props['Icon'])
            assertEquals((cs.PARAM_SECRET, 's', ''), param_map['password'])
            assertEquals('s', param_type['charset'])
            assertEquals('s', param_type['username'])
            assertEquals('s', param_type['realname'])
            assertEquals('s', param_type['server'])
            assertEquals(cs.PARAM_HAS_DEFAULT, param_flags['server'])

            assertEquals('smcv@irc.debian.org',
                    protocol_iface.IdentifyAccount({
                        'account': 'smcv',
                        'server': 'irc.debian.org'}))
        elif name == 'myspace':
            assertEquals('x-myspace', flat_props['VCardField'])
            assertEquals('im-myspace', flat_props['Icon'])
            assertEquals('s', param_type['server'])
        elif name == 'yahoo':
            assertEquals('x-yahoo', flat_props['VCardField'])
            assertEquals('im-yahoo', flat_props['Icon'])
            assertEquals('s', param_type['charset'])
        elif name == 'yahoojp':
            assertEquals('x-yahoo', flat_props['VCardField'])
            assertEquals('im-yahoojp', flat_props['Icon'])
            assertEquals('s', param_type['charset'])
        elif name == 'aim':
            assertEquals('x-aim', flat_props['VCardField'])
            assertEquals('im-aim', flat_props['Icon'])
            assertEquals('s', param_type['server'])
        elif name == 'msn':
            assertEquals('x-msn', flat_props['VCardField'])
            assertEquals('im-msn', flat_props['Icon'])
            assertEquals('s', param_type['server'])
        elif name == 'jabber':
            assertEquals('x-jabber', flat_props['VCardField'])
            assertEquals('im-jabber', flat_props['Icon'])
            assertDoesNotContain('require_tls', param_map)
            assertDoesNotContain('connect_server', param_map)
            assertEquals((cs.PARAM_SECRET, 's', ''), param_map['password'])
            assertEquals((cs.PARAM_HAS_DEFAULT, 'b', True),
                    param_map['require-encryption'])

            assertEquals('billg@example.com',
                    protocol_iface.IdentifyAccount({
                        'account': 'billg@example.com',
                        'password': 'letmein'}))
            assertEquals(r'billg@example.com',
                    protocol_iface.IdentifyAccount({
                        'account': 'billg@example.com',
                        'server': r'corp.example.com',
                        'password': 'letmein'}))

            # this contains an unsupported parameter
            call_async(q, protocol_iface, 'IdentifyAccount',
                    { 'account': 'billg@example.com',
                        'embrace-and-extend': r'WORKGROUP\Bill',
                        'password': 'letmein'})
            q.expect('dbus-error', name=cs.INVALID_ARGUMENT)
        elif name == 'qq':
            assertEquals('x-qq', flat_props['VCardField'])
            assertEquals('im-qq', flat_props['Icon'])
        elif name == 'sametime':
            assertEquals('x-sametime', flat_props['VCardField'])
            assertEquals('im-sametime', flat_props['Icon'])
        elif name == 'zephyr':
            assertEquals('x-zephyr', flat_props['VCardField'])
            assertEquals('im-zephyr', flat_props['Icon'])
            assertEquals('s', param_type['realm'])
            assertEquals('s', param_type['charset'])
        elif name == 'local-xmpp':
            # makes very little sense in an address book
            assertEquals('', flat_props['VCardField'])
            assertEquals('im-local-xmpp', flat_props['Icon'])
            assertEquals((cs.PARAM_REQUIRED, 's', ''), param_map['first-name'])
            assertDoesNotContain('first', param_map)
            assertEquals((cs.PARAM_REQUIRED, 's', ''), param_map['last-name'])
            assertDoesNotContain('last', param_map)
            assertEquals((0, 's', ''), param_map['email'])
            assertEquals((0, 's', ''), param_map['jid'])
        elif name == 'icq':
            assertEquals('x-icq', flat_props['VCardField'])
            assertEquals('im-icq', flat_props['Icon'])
        elif name == 'groupwise':
            assertEquals('x-groupwise', flat_props['VCardField'])
            assertEquals('im-groupwise', flat_props['Icon'])
        elif name == 'sipe':
            assertEquals('im-sipe', flat_props['Icon'])
            assertDoesNotContain('usersplit1', param_map)
            assertEquals((cs.PARAM_HAS_DEFAULT, 's', ''), param_map['login'])

            assertEquals('billg@example.com,',
                    protocol_iface.IdentifyAccount({
                        'account': 'billg@example.com',
                        'password': 'letmein'}))
            assertEquals(r'billg@example.com,WORKGROUP\Bill',
                    protocol_iface.IdentifyAccount({
                        'account': 'billg@example.com',
                        'login': r'WORKGROUP\Bill',
                        'password': 'letmein'}))

    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged', args=[1, 1])
    conn.Disconnect()
    q.expect('dbus-signal', signal='StatusChanged', args=[2, 1])

if __name__ == '__main__':
    exec_test(test)

