# OMcache - a memcached client library
#
# Copyright (c) 2013-2014, Oskari Saarenmaa <os@ohmu.fi>
# All rights reserved.
#
# This file is under the 2-clause BSD license.
# See the file `LICENSE` for details.

import cffi
import os

_ffi = cffi.FFI()
_ffi.cdef("""
    typedef long time_t;
    """)
_ffi.cdef(open(os.path.join(os.path.dirname(__file__), "omcache_cdef.h")).read())
_oc = _ffi.dlopen("libomcache.so.0")

_sizep = _ffi.new("size_t *")
_cucpp = _ffi.new("const unsigned char **")


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

    def _check_retval(self, ret, command):
        if ret == _oc.OMCACHE_OK or ret == _oc.OMCACHE_BUFFERED:
            return
        errstr = _ffi.string(_oc.omcache_strerror(self.omc, ret))
        raise Error("{}: {}".format(command, errstr))

    @property
    def _buffer(self):
        return _ffi.buffer(_cucpp[0], _sizep[0])[:]

    def set_buffering(self, enabled):
        ret = _oc.omcache_set_buffering(self.omc, enabled)
        self._check_retval(ret, "omcache_set_buffering")

    def reset_buffering(self):
        ret = _oc.omcache_reset_buffering(self.omc)
        self._check_retval(ret, "omcache_reset_buffering")

    def flush(self, timeout=-1):
        ret = _oc.omcache_io(self.omc, timeout, 0, _ffi.NULL)
        self._check_retval(ret, "omcache_io")

    def set(self, key, value, expiration=0, flags=0):
        ret = _oc.omcache_set(self.omc, key, len(key), value, len(value), expiration, flags)
        self._check_retval(ret, "omcache_set")

    def get(self, key):
        ret = _oc.omcache_get(self.omc, key, len(key), _cucpp, _sizep, _ffi.NULL)
        self._check_retval(ret, "omcache_get")
        return self._buffer

    def noop(self, key_for_server_selection):
        ret = _oc.omcache_noop(self.omc, key_for_server_selection, len(key_for_server_selection))
        self._check_retval(ret, "omcache_noop")

    def stat(self, key_for_server_selection):
        ret = _oc.omcache_stat(self.omc, key_for_server_selection, len(key_for_server_selection))
        self._check_retval(ret, "omcache_stat")
