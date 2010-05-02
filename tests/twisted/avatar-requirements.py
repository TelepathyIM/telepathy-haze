from servicetest import call_async, EventPattern
from hazetest import exec_test
import constants as cs

def test(q, bus, conn, stream):
    props = conn.GetAll(cs.CONN_IFACE_AVATARS,
            dbus_interface=cs.PROPERTIES_IFACE)
    types = props['SupportedAvatarMIMETypes']
    minw = props['MinimumAvatarWidth']
    minh = props['MinimumAvatarHeight']
    maxw = props['MaximumAvatarWidth']
    maxh = props['MaximumAvatarHeight']
    maxb = props['MaximumAvatarBytes']
    rech = props['RecommendedAvatarHeight']
    recw = props['RecommendedAvatarWidth']

    assert types == [], types
    assert minw == 0, minw
    assert minh == 0, minh
    assert maxw == 0, maxw
    assert maxh == 0, maxh
    assert maxb == 0, maxb
    assert recw == 0, recw
    assert rech == 0, rech

    conn.Connect()
    q.expect('dbus-signal', signal='StatusChanged',
            args=[cs.CONN_STATUS_CONNECTED, cs.CSR_REQUESTED])

    props = conn.GetAll(cs.CONN_IFACE_AVATARS,
            dbus_interface=cs.PROPERTIES_IFACE)
    types = props['SupportedAvatarMIMETypes']
    minw = props['MinimumAvatarWidth']
    minh = props['MinimumAvatarHeight']
    maxw = props['MaximumAvatarWidth']
    maxh = props['MaximumAvatarHeight']
    maxb = props['MaximumAvatarBytes']
    rech = props['RecommendedAvatarHeight']
    recw = props['RecommendedAvatarWidth']

    assert types[0] == 'image/png', types
    assert minw == 32, minw
    assert minh == 32, minh
    assert maxw == 96, maxw
    assert maxh == 96, maxh
    # libpurple currently says there's no max size
    #assert maxb == 8192, maxb
    # there's no way for libpurple to recommend a size, so we offer no opinion
    assert recw == 0, recw
    assert rech == 0, rech

    # deprecated version
    types, minw, minh, maxw, maxh, maxb = conn.Avatars.GetAvatarRequirements()
    assert types[0] == 'image/png', types
    assert minw == 32, minw
    assert minh == 32, minh
    assert maxw == 96, maxw
    assert maxh == 96, maxh
    # libpurple currently says there's no max size
    #assert maxb == 8192, maxb

if __name__ == '__main__':
    exec_test(test)
