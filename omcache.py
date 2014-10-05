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
    def __init__(self, server_list):
        self.omc = _oc.omcache_init()
        self._buffering = False
        self._conn_timeout = None
        self._reconn_timeout = None
        self._dead_timeout = None
        _oc.omcache_set_log_func(self.omc, _oc.omcache_log_stderr, _ffi.NULL)
        self.set_servers(server_list)
        self.io_timeout = 1000

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

    def set_servers(self, server_list):
        if isinstance(server_list, (list, set, tuple)):
            server_list = ",".join(server_list)
        ret = _oc.omcache_set_servers(self.omc, server_list)
        self._check_retval(ret, "omcache_set_servers")

    @property
    def connect_timeout(self):
        return self._conn_timeout

    @connect_timeout.setter
    def connect_timeout(self, msec):
        self._conn_timeout = msec
        ret = _oc.omcache_set_connect_timeout(self.omc, msec)
        self._check_retval(ret, "omcache_set_connect_timeout")

    @property
    def reconnect_timeout(self):
        return self._reconn_timeout

    @reconnect_timeout.setter
    def reconnect_timeout(self, msec):
        self._reconn_timeout = msec
        ret = _oc.omcache_set_reconnect_timeout(self.omc, msec)
        self._check_retval(ret, "omcache_set_reconnect_timeout")

    @property
    def dead_timeout(self):
        return self._dead_timeout

    @dead_timeout.setter
    def dead_timeout(self, msec):
        self._dead_timeout = msec
        ret = _oc.omcache_set_dead_timeout(self.omc, msec)
        self._check_retval(ret, "omcache_set_dead_timeout")

    @property
    def buffering(self):
        return self._buffering

    @buffering.setter
    def buffering(self, enabled):
        self._buffering = True if enabled else False
        ret = _oc.omcache_set_buffering(self.omc, enabled)
        self._check_retval(ret, "omcache_set_buffering")

    def reset_buffering(self):
        ret = _oc.omcache_reset_buffering(self.omc)
        self._check_retval(ret, "omcache_reset_buffering")

    def flush(self, timeout=-1):
        ret = _oc.omcache_io(self.omc, timeout, 0, _ffi.NULL)
        self._check_retval(ret, "omcache_io")

    def set(self, key, value, expiration=0, flags=0, cas=0):
        ret = _oc.omcache_set(self.omc, key, len(key), value, len(value), expiration, flags, cas, self.io_timeout)
        self._check_retval(ret, "omcache_set")

    def add(self, key, value, expiration=0, flags=0):
        ret = _oc.omcache_add(self.omc, key, len(key), value, len(value), expiration, flags, self.io_timeout)
        self._check_retval(ret, "omcache_add")

    def replace(self, key, value, expiration=0, flags=0):
        ret = _oc.omcache_replace(self.omc, key, len(key), value, len(value), expiration, flags, self.io_timeout)
        self._check_retval(ret, "omcache_replace")

    def delete(self, key):
        ret = _oc.omcache_delete(self.omc, key, len(key), self.io_timeout)
        self._check_retval(ret, "omcache_add")

    def get(self, key):
        ret = _oc.omcache_get(self.omc, key, len(key), _cucpp, _sizep, _ffi.NULL, self.io_timeout)
        self._check_retval(ret, "omcache_get")
        return self._buffer

    def noop(self, key_for_server_selection):
        ret = _oc.omcache_noop(self.omc, key_for_server_selection, len(key_for_server_selection), self.io_timeout)
        self._check_retval(ret, "omcache_noop")

    def stat(self, key_for_server_selection):
        ret = _oc.omcache_stat(self.omc, key_for_server_selection, len(key_for_server_selection), self.io_timeout)
        self._check_retval(ret, "omcache_stat")
