# OMcache - a memcached client library
#
# Copyright (c) 2013-2014, Oskari Saarenmaa <os@ohmu.fi>
# All rights reserved.
#
# This file is under the Apache License, Version 2.0.
# See the file `LICENSE` for details.

from collections import namedtuple
from functools import wraps
from select import select as select_select
from sys import version_info
import cffi
import logging
import os
import socket
import time

_ffi = cffi.FFI()
_ffi.cdef("""
    typedef long time_t;
    struct pollfd {
        int   fd;         /* file descriptor */
        short events;     /* requested events */
        short revents;    /* returned events */
    };
    """)
_ffi.cdef(open(os.path.join(os.path.dirname(__file__), "omcache_cdef.h")).read())
_oc = _ffi.dlopen("libomcache.so.0")

DELTA_NO_ADD = 0xffffffff

# From <bits/poll.h>
POLLIN = 1
POLLOUT = 4

# OMcache uses sys/syslog.h priority numbers
LOG_ERR = 3
LOG_WARNING = 4
LOG_NOTICE = 5
LOG_INFO = 6
LOG_DEBUG = 7

# Memcache binary protocol command opcodes
CMD_GET = 0x00
CMD_SET = 0x01
CMD_ADD = 0x02
CMD_REPLACE = 0x03
CMD_DELETE = 0x04
CMD_INCREMENT = 0x05
CMD_DECREMENT = 0x06
CMD_QUIT = 0x07
CMD_FLUSH = 0x08
CMD_GETQ = 0x09
CMD_NOOP = 0x0a
CMD_VERSION = 0x0b
CMD_GETK = 0x0c
CMD_GETKQ = 0x0d
CMD_APPEND = 0x0e
CMD_PREPEND = 0x0f
CMD_STAT = 0x10
CMD_SETQ = 0x11
CMD_ADDQ = 0x12
CMD_REPLACEQ = 0x13
CMD_DELETEQ = 0x14
CMD_INCREMENTQ = 0x15
CMD_DECREMENTQ = 0x16
CMD_QUITQ = 0x17
CMD_FLUSHQ = 0x18
CMD_APPENDQ = 0x19
CMD_PREPENDQ = 0x1a
CMD_TOUCH = 0x1c
CMD_GAT = 0x1d
CMD_GATQ = 0x1e
CMD_GATK = 0x23
CMD_GATKQ = 0x24


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


class TooLargeValueError(CommandError):
    status = _oc.OMCACHE_TOO_LARGE_VALUE


class NotStoredError(CommandError):
    status = _oc.OMCACHE_NOT_STORED


class DeltaBadValueError(CommandError):
    status = _oc.OMCACHE_DELTA_BAD_VALUE


def _to_bytes(s):
    if isinstance(s, str if version_info[0] >= 3 else unicode):
        return s.encode("utf8")
    return s


def _to_string(s):
    msg = _ffi.string(s)
    if version_info[0] >= 3 and isinstance(msg, bytes):
        msg = msg.decode("utf-8")
    return msg

if socket.htonl(1) == 1:
    def _htobe64(v):
        return v
else:
    def _htobe64(v):
        h = socket.htonl(v >> 32)
        l = socket.htonl(v & 0xffffffff)
        return (l << 32) | h


OMcacheValue = namedtuple("OMcacheValue", ["status", "key", "value", "flags", "cas", "delta_value"])


def _omc_command(func, expected_values=1):
    @wraps(func)
    def omc_async_call(self, *args, **kwargs):
        func_name = kwargs.get("func_name", func.__name__)
        timeout = kwargs.pop("timeout", self.io_timeout)
        # `objs` holds references to CFFI objects which we need to hold for a while
        req, objs = func(self, *args, **kwargs)  # pylint: disable=W0612
        if self.select == select_select:
            ret = _oc.omcache_command_status(self.omc, req, timeout)
            return self._omc_check(ret, func_name)
        resp = self._omc_command_async(req, expected_values, timeout, func_name)[0]
        return self._omc_check(resp.status, func_name)
    return omc_async_call


class OMcache(object):
    def __init__(self, server_list, log=None, select=None):
        self.omc = _oc.omcache_init()
        self._omc_log_cb = _ffi.callback("void(void*, int, const char *)", self._omc_log)
        self._log = None
        self.log = log
        self.select = select or select_select
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
    def _omc_check(ret, name, allowed=None):
        allowed = allowed if allowed is not None else [_oc.OMCACHE_BUFFERED]
        if ret == _oc.OMCACHE_OK or ret in allowed:
            return ret
        if ret == _oc.OMCACHE_NOT_FOUND:
            raise NotFoundError
        if ret == _oc.OMCACHE_KEY_EXISTS:
            raise KeyExistsError
        if ret == _oc.OMCACHE_TOO_LARGE_VALUE:
            raise TooLargeValueError
        if ret == _oc.OMCACHE_NOT_STORED:
            raise NotStoredError
        if ret == _oc.OMCACHE_DELTA_BAD_VALUE:
            raise DeltaBadValueError
        errstr = _to_string(_oc.omcache_strerror(ret))
        raise CommandError("{0}: {1}".format(name, errstr), status=ret)

    def set_servers(self, server_list):
        if isinstance(server_list, (list, set, tuple)):
            server_list = ",".join(server_list)
        return _oc.omcache_set_servers(self.omc, _to_bytes(server_list))

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
    def connect_timeout(self, msec):
        self._conn_timeout = msec
        return _oc.omcache_set_connect_timeout(self.omc, msec)

    @property
    def reconnect_timeout(self):
        return self._reconn_timeout

    @reconnect_timeout.setter
    def reconnect_timeout(self, msec):
        self._reconn_timeout = msec
        return _oc.omcache_set_reconnect_timeout(self.omc, msec)

    @property
    def dead_timeout(self):
        return self._dead_timeout

    @dead_timeout.setter
    def dead_timeout(self, msec):
        self._dead_timeout = msec
        return _oc.omcache_set_dead_timeout(self.omc, msec)

    @property
    def buffering(self):
        return self._buffering

    @buffering.setter
    def buffering(self, enabled):
        self._buffering = True if enabled else False
        return _oc.omcache_set_buffering(self.omc, enabled)

    def reset_buffers(self):
        return _oc.omcache_reset_buffers(self.omc)

    def _omc_io(self, requests, request_count, values, value_count, timeout):
        nfdsp = _ffi.new("int *")
        polltimeoutp = _ffi.new("int *")
        polls = _oc.omcache_poll_fds(self.omc, nfdsp, polltimeoutp)
        rlist, wlist = [], []
        for i in xrange(nfdsp[0]):
            if polls[i].events & POLLIN:
                rlist.append(polls[i].fd)
            if polls[i].events & POLLOUT:
                wlist.append(polls[i].fd)
        if rlist or wlist:
            if timeout <= 0:
                timeout = 0
            else:
                timeout = min(polltimeoutp[0], timeout) / 1000.0
            self.select(rlist, wlist, [], timeout)
        ret = _oc.omcache_io(self.omc, requests, request_count, values, value_count, 0)
        self._omc_check(ret, "omcache_io", allowed=[_oc.OMCACHE_AGAIN])
        if values == _ffi.NULL:
            yield OMcacheValue(ret, None, None, None, None, None)
            return
        for i in range(value_count[0]):
            key = _ffi.buffer(values[i].key, values[i].key_len)[:]
            value = _ffi.buffer(values[i].data, values[i].data_len)[:]
            yield OMcacheValue(values[i].status, key, value, values[i].flags, values[i].cas, values[i].delta_value)

    def _omc_command_async(self, requests, value_count, timeout, func_name):
        request_count = _ffi.new("size_t *")
        request_count[0] = len(requests)
        if value_count is None:
            value_count = len(requests)
        value_countp = _ffi.new("size_t *")
        values = _ffi.new("omcache_value_t[]", value_count)
        begin = time.time()
        results = []
        ret = _oc.omcache_command(self.omc, requests, request_count, _ffi.NULL, _ffi.NULL, 0)
        self._omc_check(ret, func_name, allowed=[_oc.OMCACHE_AGAIN, _oc.OMCACHE_BUFFERED])
        while request_count[0]:
            value_countp[0] = value_count
            if timeout == -1:
                time_left = 3600
            else:
                time_left = timeout - (time.time() - begin) * 1000
            if time_left < 0:
                break
            results.extend(self._omc_io(requests, request_count, values, value_countp, time_left))
        return results

    def flush(self, timeout=-1):
        if self.select == select_select:
            ret = _oc.omcache_io(self.omc, _ffi.NULL, _ffi.NULL, _ffi.NULL, _ffi.NULL, timeout)
        else:
            ret = _oc.OMCACHE_AGAIN
            time_left = 1
            begin = time.time()
            while ret == _oc.OMCACHE_AGAIN and time_left > 0:
                time_left = 3600 if timeout == -1 else timeout - (time.time() - begin) * 1000
                resps = list(self._omc_io(_ffi.NULL, _ffi.NULL, _ffi.NULL, _ffi.NULL, time_left))
                ret = resps[0].status
        return self._omc_check(ret, "flush")

    def _request(self, opcode, key=None, data=None, extra=None, cas=0, server_index=-1, objects=None, request=None):
        if objects is None:
            objects = []
        requests = None
        if request is None:
            requests = _ffi.new("omcache_req_t[]", 1)
            request = requests[0]
        request.server_index = server_index
        request.header.opcode = opcode
        if cas:
            request.header.cas = _htobe64(cas)
        bodylen = 0
        if extra:
            objects.append(extra)
            bodylen = _ffi.sizeof(extra)
            request.extra = extra
            request.header.extlen = bodylen
        if key is not None:
            if not isinstance(key, _ffi.CData):
                key = _ffi.new("unsigned char[]", key)
                objects.append(key)
            bodylen += len(key) - 1
            request.key = key
            request.header.keylen = socket.htons(len(key) - 1)
        if data is not None:
            if not isinstance(data, _ffi.CData):
                data = _ffi.new("unsigned char[]", data)
                objects.append(data)
            bodylen += len(data) - 1
            request.data = data
        request.header.bodylen = socket.htonl(bodylen)
        return requests, objects

    @_omc_command
    def noop(self, server_index=0, timeout=None):
        return self._request(CMD_NOOP, server_index=server_index)

    def stat(self, command="", server_index=0, timeout=None):
        # `objs` holds references to CFFI objects which we need to hold for a while
        req, objs = self._request(CMD_STAT, key=_to_bytes(command), server_index=server_index)  # pylint: disable=W0612
        timeout = timeout if timeout is not None else self.io_timeout
        resps = self._omc_command_async(req, 100, timeout, "stat")
        results = {}
        for resp in resps:
            self._omc_check(resp.status, "stat")
            if not resp.key and not resp.value:
                break
            results[resp.key] = resp.value
        return results

    @_omc_command
    def _omc_set(self, key, value, expiration, flags, cas, timeout, opcode, func_name):
        extra = _ffi.new("uint32_t[]", 2)
        extra[0] = socket.htonl(flags)
        extra[1] = socket.htonl(expiration)
        return self._request(CMD_SET, key=_to_bytes(key), data=_to_bytes(value), extra=extra, cas=cas)

    def set(self, key, value, expiration=0, flags=0, cas=0, timeout=None):
        return self._omc_set(key, value, expiration, flags, cas, timeout, CMD_SET, "set")

    def add(self, key, value, expiration=0, flags=0, timeout=None):
        return self._omc_set(key, value, expiration, flags, 0, timeout, CMD_ADD, "add")

    def replace(self, key, value, expiration=0, flags=0, timeout=None):
        return self._omc_set(key, value, expiration, flags, 0, timeout, CMD_REPLACE, "replace")

    def append(self, key, value, cas=0, timeout=None):
        return self._omc_set(key, value, 0, 0, cas, timeout, CMD_APPEND, "append")

    def prepend(self, key, value, cas=0, timeout=None):
        return self._omc_set(key, value, 0, 0, cas, timeout, CMD_PREPEND, "prepend")

    @_omc_command
    def delete(self, key, timeout=None):
        return self._request(CMD_DELETE, key=_to_bytes(key))

    @_omc_command
    def touch(self, key, expiration=0, timeout=None):
        extra = _ffi.new("uint32_t[]", 1)
        extra[0] = socket.htonl(expiration)
        return self._request(CMD_TOUCH, key=_to_bytes(key), extra=extra)

    def get(self, key, flags=False, cas=False, timeout=None):
        # `objs` holds references to CFFI objects which we need to hold for a while
        req, objs = self._request(CMD_GETK, _to_bytes(key))  # pylint: disable=W0612
        timeout = timeout if timeout is not None else self.io_timeout
        resp = self._omc_command_async(req, None, timeout, "get")[0]
        self._omc_check(resp.status, "get")
        if not flags and not cas:
            return resp.value
        elif flags and cas:
            return (resp.value, resp.flags, resp.cas)
        elif flags:
            return (resp.value, resp.flags)
        elif cas:
            return (resp.value, resp.cas)

    def get_multi(self, keys, flags=False, cas=False, timeout=None):
        if not isinstance(keys, (list, tuple)):
            keys = list(keys)
        objects = []
        requests = _ffi.new("omcache_req_t[]", len(keys))
        for i in range(len(keys)):
            self._request(CMD_GETKQ, _to_bytes(keys[i]), request=requests[i], objects=objects)
        timeout = timeout if timeout is not None else self.io_timeout
        resps = self._omc_command_async(requests, None, timeout, "get_multi")
        results = {}
        for resp in resps:
            if resp.status != _oc.OMCACHE_OK:
                continue
            if flags and cas:
                results[resp.key] = (resp.value, resp.flags, resp.cas)
            elif flags:
                results[resp.key] = (resp.value, resp.flags)
            elif cas:
                results[resp.key] = (resp.value, resp.cas)
            else:
                results[resp.key] = resp.value
        return results

    def _omc_delta(self, key, delta, initial, expiration, timeout, func_name):
        # Delta operation definition in the protocol is a bit weird; if
        # 'expiration' is set to DELTA_NO_ADD (0xffffffff) the value will
        # not be created if it doesn't exist yet, but since we have a
        # 'initial' argument in the python api we'd really like to use it to
        # signal whether or not a value should be initialized.
        #
        # To do that we'll map expiration to DELTA_NO_ADD if initial is None
        # and expiration is empty and throw an error if initial is None but
        # expiration isn't empty.

        if delta < 0:
            opcode = CMD_DECREMENT
            delta = -delta
        else:
            opcode = CMD_INCREMENT
        if initial is None:
            if expiration:
                raise Error(func_name + " operation's initial must be set if expiration time is used")
            expiration = DELTA_NO_ADD
            initial = 0
        # CFFI doesn't support packed structs before version 0.8.2 so we create
        # an array of 5 x uint32_s instead of 2 x uint64_t + 1 x uint32_t
        extra = _ffi.new("uint32_t[]", 5)
        extra[0] = socket.htonl(delta >> 32)
        extra[1] = socket.htonl(delta & 0xffffffff)
        if initial:
            extra[2] = socket.htonl(initial >> 32)
            extra[3] = socket.htonl(initial & 0xffffffff)
        extra[4] = socket.htonl(expiration)
        # `objs` holds references to CFFI objects which we need to hold for a while
        req, objs = self._request(opcode, key=_to_bytes(key), extra=extra)  # pylint: disable=W0612
        timeout = timeout if timeout is not None else self.io_timeout
        resps = self._omc_command_async(req, 1, timeout, func_name)
        self._omc_check(resps[0].status, func_name)
        return resps[0].delta_value

    def increment(self, key, delta=1, initial=None, expiration=0, timeout=None):
        return self._omc_delta(key, delta, initial, expiration, timeout, "increment")

    def decrement(self, key, delta=1, initial=None, expiration=0, timeout=None):
        return self._omc_delta(key, -delta, initial, expiration, timeout, "decrement")
