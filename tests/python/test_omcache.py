import errno
import omcache
import select
from pytest import raises  # pylint: disable=E0611
from . import OMcacheCase


class TestOmcache(OMcacheCase):
    def test_internal_utils(self):
        err = select.error(errno.EINTR, "interrupted")
        assert omcache._select_errno(err) == errno.EINTR

    def test_set_servers(self):
        servers = [self.get_memcached(), self.get_memcached()]
        oc = omcache.OMcache(servers, self.log)
        oc.set_servers(servers + ["127.0.0.1:2", "127.0.0.1:113333", "127.0.0.1:113000"])
        oc.set_servers(servers * 8)
        oc.set_servers(servers)

    def test_stat(self):
        servers = [self.get_memcached(), self.get_memcached()]
        oc = omcache.OMcache(servers, self.log)
        s1 = oc.stat("settings", 0)
        s2 = oc.stat("settings", 1)
        assert s1 != s2
        s1 = oc.stat("", 0)
        s2 = oc.stat("", 1)
        assert s1 != s2
        assert len(s1) > 30
        assert len(s2) > 30

    def test_incr_decr(self):
        oc = omcache.OMcache([self.get_memcached()], self.log)
        with raises(omcache.NotFoundError):
            oc.increment("test_incr_decr", 2, initial=None)
        assert oc.increment("test_incr_decr", 2, initial=0) == 0
        assert oc.increment("test_incr_decr", 2) == 2
        assert oc.increment("test_incr_decr", 2) == 4
        assert oc.increment("test_incr_decr", 2, initial=42) == 6
        assert oc.decrement("test_incr_decr", 5) == 1
        assert oc.decrement("test_incr_decr", 5) == 0
        oc.set("test_incr_decr", "567")
        assert oc.decrement("test_incr_decr", 5) == 562
        oc.set("test_incr_decr", "x567")
        with raises(omcache.DeltaBadValueError):
            oc.decrement("test_incr_decr", 5)

    def test_add_replace_set_delete(self):
        oc = omcache.OMcache([self.get_memcached(), self.get_memcached()], self.log)
        with raises(omcache.NotFoundError):
            oc.replace("test_arsd", "replaced")
        with raises(omcache.NotFoundError):
            oc.get("test_arsd")
        oc.add("test_arsd", "added")
        assert oc.get("test_arsd") == "added"
        oc.set("test_arsd", "set")
        assert oc.get("test_arsd") == "set"
        oc.replace("test_arsd", "replaced")
        assert oc.get("test_arsd") == "replaced"
        with raises(omcache.KeyExistsError):
            oc.add("test_arsd", "foobar")
        assert oc.get("test_arsd") == "replaced"
