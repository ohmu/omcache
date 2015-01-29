import errno
import omcache
import random
import select
from pytest import raises  # pylint: disable=E0611
from time import sleep
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
        oc.noop(0)
        oc.noop(1)

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
        oc.set("test_incr_decr", "100")
        assert oc.decrement("test_incr_decr", -5) == 105
        assert oc.increment("test_incr_decr", -5) == 100
        with raises(omcache.Error):
            oc.increment("test_incr_decr_e", 2, expiration=42, initial=None)

    def test_add_replace_set_delete(self):
        oc = omcache.OMcache([self.get_memcached(), self.get_memcached()], self.log)
        with raises(omcache.NotFoundError):
            oc.replace("test_arsd", "replaced")
        with raises(omcache.NotFoundError):
            oc.get("test_arsd")
        oc.add("test_arsd", "added")
        assert oc.get("test_arsd") == b"added"
        oc.set("test_arsd", "set")
        assert oc.get("test_arsd") == b"set"
        oc.replace("test_arsd", "replaced")
        assert oc.get("test_arsd") == b"replaced"
        with raises(omcache.KeyExistsError):
            oc.add("test_arsd", "foobar")
        assert oc.get("test_arsd") == b"replaced"
        oc.delete("test_arsd")
        with raises(omcache.NotFoundError):
            oc.delete("test_arsd")
        oc.set("test_arsd", "arsd", flags=531)
        res, flags, cas = oc.get("test_arsd", flags=True, cas=True)  # pylint: disable=W0632
        assert res == b"arsd"
        assert flags == 531
        assert cas > 0
        res, flags = oc.get("test_arsd", flags=True)  # pylint: disable=W0632
        assert flags == 531

    def test_cas(self):
        oc = omcache.OMcache([self.get_memcached()], self.log)
        with raises(omcache.NotFoundError):
            oc.set("test_cas", "xxx", cas=42424242)
        oc.set("test_cas", "xxx")
        with raises(omcache.KeyExistsError):
            oc.set("test_cas", "xxx", cas=42424242)
        res, cas1 = oc.get("test_cas", cas=True)  # pylint: disable=W0632
        assert res == b"xxx"
        assert cas1 > 0
        oc.set("test_cas", "42", cas=cas1)
        with raises(omcache.KeyExistsError):
            oc.set("test_cas", "zzz", cas=cas1)
        res, cas2 = oc.get("test_cas", cas=True)  # pylint: disable=W0632
        assert res == b"42"
        assert cas2 > 0
        assert cas2 != cas1
        oc.increment("test_cas", 8)
        with raises(omcache.KeyExistsError):
            oc.set("test_cas", "zzz", cas=cas2)
        res, cas3 = oc.get("test_cas", cas=True)  # pylint: disable=W0632
        assert res == b"50"
        assert cas3 > 0
        assert cas3 != cas2

    def test_touch(self):
        oc = omcache.OMcache([self.get_memcached()], self.log)
        oc.set("test_touch", "qwerty", expiration=2)
        assert oc.get("test_touch") == b"qwerty"
        sleep(2)
        with raises(omcache.NotFoundError):
            oc.get("test_touch")
        oc.set("test_touch", "qwerty", expiration=1)
        assert oc.get("test_touch") == b"qwerty"
        oc.touch("test_touch", expiration=3)
        sleep(2)
        assert oc.get("test_touch") == b"qwerty"

    def test_append_prepend(self):
        oc = omcache.OMcache([self.get_memcached()], self.log)
        oc.set("test_ap", "asdf")
        assert oc.get("test_ap") == b"asdf"
        oc.append("test_ap", "zxcvb")
        assert oc.get("test_ap") == b"asdfzxcvb"
        oc.prepend("test_ap", "qwerty")
        assert oc.get("test_ap") == b"qwertyasdfzxcvb"

    def test_multi(self):
        oc = omcache.OMcache([self.get_memcached(), self.get_memcached()], self.log)
        item_count = 123
        val = str(random.random()).encode("utf-8")
        for i in range(item_count):
            oc.set("test_multi_{0}".format(i * 2), val, flags=i)
        keys = ["test_multi_{0}".format(i) for i in range(item_count * 2)]
        random.shuffle(keys)
        results = oc.get_multi(keys)
        assert len(results) == item_count
        for i in range(item_count):
            assert results["test_multi_{0}".format(i * 2).encode("utf-8")] == val
        # test with flags and cas
        results = oc.get_multi(keys, flags=True)
        assert len(results) == item_count
        for i in range(item_count):
            assert results["test_multi_{0}".format(i * 2).encode("utf-8")] == (val, i)
        results = oc.get_multi(keys, cas=True)
        assert len(results) == item_count
        # count the number of distinct cas values, we can't just compare
        # them to the previous entry as we're using two memcache servers
        # which may use the same cas values
        casses = set()
        for i in range(item_count):
            res, cas = results["test_multi_{0}".format(i * 2).encode("utf-8")]
            assert res == val
            casses.add(cas)
        assert len(casses) > item_count / 3
        results = oc.get_multi(keys, cas=True, flags=True)
        assert len(results) == item_count
        casses = set()
        for i in range(item_count):
            res, flags, cas = results["test_multi_{0}".format(i * 2).encode("utf-8")]
            assert res == val
            assert flags == i
            casses.add(cas)
        assert len(casses) > item_count / 3

    def test_dist_methods(self):
        # just make sure the different distribution methods distribute keys, well, differently
        mc1 = self.get_memcached()
        mc2 = self.get_memcached()
        oc = omcache.OMcache([mc1, mc2], self.log)
        item_count = 123
        for i in range(item_count):
            oc.set("test_dist_{0}".format(i), "orig_dist")
        oc.flush()
        oc.set_distribution_method("libmemcached_ketama")
        for i in range(item_count):
            oc.set("test_dist_{0}".format(i), "ketama")
        oc.flush()
        oc.set_distribution_method("libmemcached_ketama_weighted")
        for i in range(item_count):
            oc.set("test_dist_{0}".format(i), "ketama_weighted")
        oc.flush()
        oc.set_distribution_method("libmemcached_ketama_pre1010")
        for i in range(item_count):
            oc.set("test_dist_{0}".format(i), "ketama_pre1010")
        oc.flush()
        with raises(omcache.Error):
            oc.set_distribution_method("xxx")
        keys = ["test_dist_{0}".format(i) for i in range(item_count)]
        oc.set_servers([mc1])
        results = list(oc.get_multi(keys).values())
        oc.set_servers([mc2])
        results.extend(oc.get_multi(keys).values())
        counts = {}
        for value in results:
            if value not in counts:
                counts[value] = 0
            counts[value] += 1
        assert set([b"ketama", b"ketama_weighted", b"ketama_pre1010"]).issuperset(counts)
        assert counts[b"ketama"] >= item_count / 10
        assert counts[b"ketama_weighted"] >= item_count / 10
        assert counts[b"ketama_pre1010"] >= item_count / 10
