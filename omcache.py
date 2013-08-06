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
_ffi.cdef("typedef long time_t;")
_ffi.cdef(open(os.path.join(os.path.dirname(__file__), "omcache_cdef.h")).read())
_oc = _ffi.dlopen("libomcache.so.0")

class Error(Exception):
    """OMcache error"""

class OMcache(object):
    def __init__(self, servers):
        self.omc = _oc.omcache_init()
        _oc.omcache_set_servers(self.omc, servers)

    def __del__(self):
        omc = getattr(self, "omc", None)
        if omc is not None:
            _oc.omcache_free(omc)
            self.omc = None

    def set_distribution(self, dist):
        if dist == "ketama":
            _oc.omcache_set_dist_func(self.omc,
                _oc.omcache_dist_ketama_init,
                _oc.omcache_dist_ketama_free,
                _oc.omcache_dist_ketama_lookup, self.omc)
        elif dist == "modulo":
            _oc.omcache_set_dist_func(self.omc, _ffi.NULL, _ffi.NULL,
                _oc.omcache_dist_modulo_lookup, self.omc)
        else:
            raise Error("unknown distribution function {0!r}".format(dist))

    def flush_buffers(self, timeout=0):
        return _oc.omcache_flush_buffers(self.omc, timeout)

    def set(self, key, value, expiration=0, flags=0):
        return _oc.omcache_set(self.omc, key, len(key), value, len(value), expiration, flags)
