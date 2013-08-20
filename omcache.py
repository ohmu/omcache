# OMcache - a memcached client library
#
# Copyright (c) 2013, Oskari Saarenmaa <os@ohmu.fi>
# All rights reserved.
#
# This file is under the 2-clause BSD license.
# See the file `LICENSE` for details.

import cffi
import os

_ffi = cffi.FFI()
_ffi.cdef("""
    typedef long time_t;
    struct iovec {
        void  *iov_base;    /* Starting address */
        size_t iov_len;     /* Number of bytes to transfer */
    };
    """)
_ffi.cdef(open(os.path.join(os.path.dirname(__file__), "omcache_cdef.h")).read())
_oc = _ffi.dlopen("libomcache.so.0")

class Error(Exception):
    """OMcache error"""

class OMcache(object):
    def __init__(self, servers):
        self.omc = _oc.omcache_init()
        _oc.omcache_set_servers(self.omc, servers)
        _oc.omcache_set_log_func(self.omc, _oc.omcache_log_stderr, _ffi.NULL)

    def __del__(self):
        omc = getattr(self, "omc", None)
        if omc is not None:
            _oc.omcache_free(omc)
            self.omc = None

    def set_distribution(self, dist):
        if dist == "ketama":
            return _oc.omcache_set_dist_func(self.omc,
                _oc.omcache_dist_ketama_init,
                _oc.omcache_dist_ketama_free,
                _oc.omcache_dist_ketama_lookup, self.omc)
        elif dist == "modulo":
            return _oc.omcache_set_dist_func(self.omc,
                _ffi.NULL, _ffi.NULL,
                _oc.omcache_dist_modulo_lookup, self.omc)
        else:
            raise Error("unknown distribution function {0!r}".format(dist))

    def set_buffering(self, enabled):
        return _oc.omcache_set_buffering(self.omc, enabled)

    def set_replication(self, replicas):
        return _oc.omcache_set_replication(self.omc, replicas)

    def reset_buffering(self):
        return _oc.omcache_reset_buffering(self.omc)

    def flush_buffers(self, timeout=-1):
        return _oc.omcache_flush_buffers(self.omc, timeout)

    def set(self, key, value, expiration=0, flags=0):
        return _oc.omcache_set(self.omc, key, len(key), value, len(value), expiration, flags)

    def noop(self, key_for_server_selection):
        return _oc.omcache_noop(self.omc, key_for_server_selection, len(key_for_server_selection))

    def stat(self, key_for_server_selection):
        return _oc.omcache_stat(self.omc, key_for_server_selection, len(key_for_server_selection))

    def read(self, timeout=0):
        iov = _ffi.new("struct iovec[]", 1)
        ret = _oc.omcache_read(self.omc, iov, 1, timeout)
