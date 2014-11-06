# OMcache - a memcached client library
#
# Copyright (c) 2013-2014, Oskari Saarenmaa <os@ohmu.fi>
# All rights reserved.
#
# This file is under the Apache License, Version 2.0.
# See the file `LICENSE` for details.

from functools import wraps
from sys import version_info
import cffi
import logging
import os

_ffi = cffi.FFI()
_ffi.cdef("""
    typedef long time_t;
    """)
_ffi.cdef(open(os.path.join(os.path.dirname(__file__), "omcache_cdef.h")).read())
_oc = _ffi.dlopen("libomcache.so.0")

_sizep = _ffi.new("size_t *")
_sizep2 = _ffi.new("size_t *")
_u32p = _ffi.new("uint32_t *")
_u64p = _ffi.new("uint64_t *")
_cucpp = _ffi.new("const unsigned char **")

DELTA_NO_ADD = 0xffffffff

# OMcache uses sys/syslog.h priority numbers
LOG_ERR = 3
LOG_WARNING = 4
LOG_NOTICE = 5
LOG_INFO = 6
LOG_DEBUG = 7


class Error(Exception):
    """OMcache error"""


class CommandError(Error):
    status = None

    def __init__(self, msg=None, status=None):
        super(CommandError, self).__init__(msg)
        if status is not None:
            self.status = status


class NotFoundError(CommandError):
    status = _oc.OMCACHE_NOT_FOUND


class KeyExistsError(CommandError):
    status = _oc.OMCACHE_KEY_EXISTS


def _to_bytes(s):
    if isinstance(s, str if version_info[0] >= 3 else unicode):
        return s.encode("utf8")
    return s

def _to_string(s):
    msg = _ffi.string(s)
    if version_info[0] >= 3 and isinstance(msg, bytes):
        msg = msg.decode("utf-8")
    return msg


class OMcache(object):
    def __init__(self, server_list, log=None):
        self.omc = _oc.omcache_init()
        self._omc_log_cb = _ffi.callback("void(void*, int, const char *)", self._omc_log)
        self.log = log
        self._buffering = False
        self._conn_timeout = None
        self._reconn_timeout = None
        self._dead_timeout = None
        self.set_servers(server_list)
        self.io_timeout = 1000

    def __del__(self):
        omc = getattr(self, "omc", None)
        if omc is not None:
            _oc.omcache_free(omc)
            self.omc = None

    def _omc_log(self, context, level, msg):
        msg = _to_string(msg)
        if level <= LOG_ERR:
            self._log.error(msg)
        elif level == LOG_WARNING:
            self._log.warning(msg)
        elif level == LOG_DEBUG:
            self._log.debug(msg)
        else:
            self._log.info(msg)

    @property
    def log(self):
        return self._log

    @log.setter
    def log(self, log):
        self._log = log
        log_cb = self._omc_log_cb if log else _ffi.NULL
        level = 0
        if log and hasattr(log, "getEffectiveLevel"):
            pyloglevel = log.getEffectiveLevel()
            if pyloglevel <= logging.DEBUG:
                level = LOG_DEBUG
            elif pyloglevel <= logging.INFO:
                level = LOG_INFO
            elif pyloglevel < logging.WARNING:
                # NOTICE is something between INFO and WARNING, not defined
                # in Python by default.
                level = LOG_NOTICE
            elif pyloglevel <= logging.WARNING:
                level = LOG_WARNING
            elif pyloglevel <= logging.ERROR:
                level = LOG_ERR
            else:
                # OMcache doesn't use anything more severe than LOG_ERR
                level = LOG_ERR - 1
        _oc.omcache_set_log_callback(self.omc, level, log_cb, _ffi.NULL)

    @staticmethod
    def _omc_check(name, ret, return_buffer=False, allowed=[_oc.OMCACHE_BUFFERED]):
        if return_buffer:
            if ret == _oc.OMCACHE_OK:
                return _ffi.buffer(_cucpp[0], _sizep[0])[:]
        elif ret == _oc.OMCACHE_OK or ret in allowed:
            return ret
        if ret == _oc.OMCACHE_NOT_FOUND:
            raise NotFoundError
        if ret == _oc.OMCACHE_KEY_EXISTS:
            raise KeyExistsError
        errstr = _to_string(_oc.omcache_strerror(ret))
        raise CommandError("{0}: {1}".format(name, errstr), status=ret)

    def _omc_return(self, name=None, return_buffer=False):
        def decorate(func):
            @wraps(func)
            def check_rc_wrapper(self, *args, **kwargs):
                ret = func(self, *args, **kwargs)
                return self._omc_check(name or func.__name__, ret, return_buffer)
            return check_rc_wrapper
        return decorate

    @_omc_return("omcache_set_servers")
    def set_servers(self, server_list):
        if isinstance(server_list, (list, set, tuple)):
            server_list = ",".join(server_list)
        return _oc.omcache_set_servers(self.omc, _to_bytes(server_list))

    @_omc_return("omcache_set_distribution_method")
    def set_distribution_method(self, method):
        if method == "libmemcached_ketama":
            ms = _ffi.addressof(_oc.omcache_dist_libmemcached_ketama)
        elif method == "libmemcached_ketama_weighted":
            ms = _ffi.addressof(_oc.omcache_dist_libmemcached_ketama_weighted)
        elif method == "libmemcached_ketama_pre1010":
            ms = _ffi.addressof(_oc.omcache_dist_libmemcached_ketama_pre1010)
        else:
            raise Error("invalid distribution method {0!r}".format(method))
        return _oc.omcache_set_distribution_method(self.omc, ms)

    @property
    def connect_timeout(self):
        return self._conn_timeout

    @connect_timeout.setter
    @_omc_return("omcache_set_connect_timeout")
    def connect_timeout(self, msec):
        self._conn_timeout = msec
        return _oc.omcache_set_connect_timeout(self.omc, msec)

    @property
    def reconnect_timeout(self):
        return self._reconn_timeout

    @reconnect_timeout.setter
    @_omc_return("omcache_set_reconnect_timeout")
    def reconnect_timeout(self, msec):
        self._reconn_timeout = msec
        return _oc.omcache_set_reconnect_timeout(self.omc, msec)

    @property
    def dead_timeout(self):
        return self._dead_timeout

    @dead_timeout.setter
    @_omc_return("omcache_set_dead_timeout")
    def dead_timeout(self, msec):
        self._dead_timeout = msec
        return _oc.omcache_set_dead_timeout(self.omc, msec)

    @property
    def buffering(self):
        return self._buffering

    @buffering.setter
    @_omc_return("omcache_set_buffering")
    def buffering(self, enabled):
        self._buffering = True if enabled else False
        return _oc.omcache_set_buffering(self.omc, enabled)

    @_omc_return("omcache_reset_buffers")
    def reset_buffers(self):
        return _oc.omcache_reset_buffers(self.omc)

    @_omc_return("omcache_io")
    def flush(self, timeout=-1):
        return _oc.omcache_io(self.omc, _ffi.NULL, _ffi.NULL, _ffi.NULL, _ffi.NULL, timeout)

    @_omc_return("omcache_noop")
    def noop(self, server_index=0, timeout=None):
        timeout = timeout if timeout is not None else self.io_timeout
        return _oc.omcache_noop(self.omc, server_index, timeout)

    def stat(self, command="", server_index=0, timeout=None):
        command = _to_bytes(command)
        timeout = timeout if timeout is not None else self.io_timeout
        stat_count = 50  # 50 keys enough?
        values = _ffi.new("omcache_value_t[]", stat_count)
        _sizep[0] = stat_count
        ret = _oc.omcache_stat(self.omc, command, values, _sizep, server_index, timeout)
        self._omc_check("omcache_stat", ret)
        results = {}
        for i in range(_sizep[0]):
            self._omc_check("omcache_stat", values[i].status)
            key = _ffi.buffer(values[i].key, values[i].key_len)[:]
            value = _ffi.buffer(values[i].data, values[i].data_len)[:]
            if not key and not value:
                break
            results[key] = value
        return results

    @_omc_return("omcache_set")
    def set(self, key, value, expiration=0, flags=0, cas=0, timeout=None):
        key = _to_bytes(key)
        value = _to_bytes(value)
        timeout = timeout if timeout is not None else self.io_timeout
        return _oc.omcache_set(self.omc, key, len(key), value, len(value), expiration, flags, cas, timeout)

    @_omc_return("omcache_add")
    def add(self, key, value, expiration=0, flags=0, timeout=None):
        key = _to_bytes(key)
        value = _to_bytes(value)
        timeout = timeout if timeout is not None else self.io_timeout
        return _oc.omcache_add(self.omc, key, len(key), value, len(value), expiration, flags, timeout)

    @_omc_return("omcache_replace")
    def replace(self, key, value, expiration=0, flags=0, timeout=None):
        key = _to_bytes(key)
        value = _to_bytes(value)
        timeout = timeout if timeout is not None else self.io_timeout
        return _oc.omcache_replace(self.omc, key, len(key), value, len(value), expiration, flags, timeout)

    @_omc_return("omcache_delete")
    def delete(self, key, timeout=None):
        key = _to_bytes(key)
        timeout = timeout if timeout is not None else self.io_timeout
        return _oc.omcache_delete(self.omc, key, len(key), timeout)

    def get(self, key, flags=False, cas=False, timeout=None):
        key = _to_bytes(key)
        timeout = timeout if timeout is not None else self.io_timeout
        ret = _oc.omcache_get(self.omc, key, len(key), _cucpp, _sizep,
                              _u32p if flags else _ffi.NULL,
                              _u64p if cas else _ffi.NULL, timeout)
        buf = self._omc_check("omcache_get", ret, return_buffer=True)
        if not flags and not cas:
            return buf
        elif flags and cas:
            return (buf, _u32p[0], _u64p[0])
        elif flags:
            return (buf, _u32p[0])
        elif cas:
            return (buf, _u64p[0])

    def get_multi(self, keys, flags=False, cas=False, timeout=None):
        keys = [_to_bytes(key) for key in keys]
        ckeys = [_ffi.new("unsigned char[]", key) for key in keys]
        key_lens = [len(key) for key in keys]
        # send all requests but don't look for responses yet
        requests = _ffi.new("omcache_req_t[]", len(keys))
        request_count = _sizep2
        request_count[0] = len(keys)
        ret = _oc.omcache_get_multi(self.omc, ckeys, key_lens, len(keys),
                                    requests, request_count, _ffi.NULL, _ffi.NULL, 0)
        self._omc_check("omcache_get_multi", ret, allowed=[_oc.OMCACHE_OK, _oc.OMCACHE_AGAIN, _oc.OMCACHE_BUFFERED])
        # now look for responses
        timeout = timeout if timeout is not None else self.io_timeout
        values = _ffi.new("omcache_value_t[]", len(keys))
        value_count = _sizep
        results = {}
        while request_count[0]:
            value_count[0] = len(keys)
            ret = _oc.omcache_io(self.omc, requests, request_count, values, value_count, timeout)
            self._omc_check("omcache_io", ret, allowed=[_oc.OMCACHE_OK, _oc.OMCACHE_AGAIN])
            for i in range(value_count[0]):
                if values[i].status != _oc.OMCACHE_OK:
                    continue
                key = _ffi.buffer(values[i].key, values[i].key_len)[:]
                value = _ffi.buffer(values[i].data, values[i].data_len)[:]
                if flags and cas:
                    value = (value, values[i].flags, values[i].cas)
                elif flags:
                    value = (value, values[i].flags)
                elif cas:
                    value = (value, values[i].cas)
                results[key] = value
        return results

    def increment(self, key, delta=1, initial=0, expiration=0, timeout=None):
        key = _to_bytes(key)
        timeout = timeout if timeout is not None else self.io_timeout
        ret = _oc.omcache_increment(self.omc, key, len(key), delta, initial, expiration, _u64p, timeout)
        ret = self._omc_check("omcache_increment", ret)
        if ret != _oc.OMCACHE_OK:
            return None
        return _u64p[0]

    def decrement(self, key, delta=1, initial=0, expiration=0, timeout=None):
        key = _to_bytes(key)
        timeout = timeout if timeout is not None else self.io_timeout
        ret = _oc.omcache_decrement(self.omc, key, len(key), delta, initial, expiration, _u64p, timeout)
        ret = self._omc_check("omcache_decrement", ret)
        if ret != _oc.OMCACHE_OK:
            return None
        return _u64p[0]
