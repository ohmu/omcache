# omcache_pylibmc.py - a kludgy pylbmc API compatibility layer
#
# Copyright (c) 2013-2014, Oskari Saarenmaa <os@ohmu.fi>
# All rights reserved.
#
# This file is under the Apache License, Version 2.0.
# See the file `LICENSE` for details.
#
# NOTE: The functionality provided by this wrapper is limited and it is not
# supported; it's provided to make it easier to prototype OMcache in simple
# programs that use the pylibmc API.

from sys import version_info
import omcache
import warnings


MemcachedError = omcache.CommandError
NotFound = omcache.NotFoundError

PYLIBMC_FLAG_PICKLE = 0x01  # not supported
PYLIBMC_FLAG_INT = 0x02
PYLIBMC_FLAG_LONG = 0x04
PYLIBMC_FLAG_ZLIB = 0x08  # not supported
PYLIBMC_FLAG_BOOL = 0x10


if version_info[0] >= 3:
    _i_types = int
    _u_type = str
else:
    _i_types = (int, long)
    _u_type = unicode

def _s_value(value):
    flags = 0
    if isinstance(value, bool):
        flags |= PYLIBMC_FLAG_BOOL
        value = str(value)
    elif isinstance(value, _i_types):
        flags |= PYLIBMC_FLAG_INT
        value = str(value)
    if isinstance(value, _u_type):
        value = value.encode("utf-8")
    elif not isinstance(value, bytes):
        raise ValueError("Can't store value of type {0!r}".format(type(value)))
    return value, flags


class Client(omcache.OMcache):
    def __init__(self, servers, behaviors=None, binary=None, username=None, password=None):
        if username or password:
            raise omcache.Error("OMcache does not support authentication at the moment")
        if binary is False:
            warnings.warn("OMcache always uses binary protocol, ignoring binary=False")
        super(Client, self).__init__(servers)
        self._behaviors = {}
        if behaviors:
            self.behaviors = behaviors

    @property
    def behaviors(self):
        return self._behaviors

    @behaviors.setter
    def behaviors(self, behaviors):
        for k, v in behaviors.items():
            if k in ("cas", "no_block", "remove_failed", "auto_eject", "failure_limit"):
                pass
            elif k == "dead_timeout":
                self.dead_timeout = v * 1000  # seconds in pylibmc
            elif k == "retry_timeout":
                self.reconnect_timeout = v * 1000  # seconds in pylibmc
            elif k == "connect_timeout":
                self.connect_timeout = v  # milliseconds in pylibmc
            elif k in ("ketama", "ketama_weighted", "ketama_pre1010") and v:
                self.set_distribution_method("libmemcached_" + k)
            else:
                warnings.warn("OMcache does not support behavior {0!r}: {1!r}".format(k, v))
                continue
            self._behaviors[k] = v

    incr = omcache.OMcache.increment
    decr = omcache.OMcache.decrement

    @staticmethod
    def _deserialize_value(value, flags):
        if flags & (PYLIBMC_FLAG_PICKLE | PYLIBMC_FLAG_ZLIB):
            warnings.warn("Ignoring cache value {0!r} with unsupported flags 0x{1:x}".format(value, flags))
            return None
        elif flags & (PYLIBMC_FLAG_INT | PYLIBMC_FLAG_LONG):
            return int(value)
        elif flags & PYLIBMC_FLAG_BOOL:
            return bool(value)
        return value

    def get(self, key, cas=False):
        try:
            value, flags, casval = super(Client, self).get(key, cas=True, flags=True)
        except NotFound:
            return None
        value = self._deserialize_value(value, flags)
        if cas:
            return (value, casval)
        return value

    def gets(self, key):
        return self.get(key, cas=True)

    def get_multi(self, keys, key_prefix=None):
        if key_prefix:
            keys = ["{0}{1}".format(key_prefix, key) for key in keys]
        values = super(Client, self).get_multi(keys, flags=True)
        result = {}
        for key, (value, flags) in values.items():
            if key_prefix:
                key = key[len(key_prefix):]
            result[key] = self._deserialize_value(value, flags)
        return result

    def set(self, key, value, time=0):
        value, flags = _s_value(value)
        super(Client, self).set(key, value, expiration=time, flags=flags)
        return True

    def add(self, key, value, time=0):
        value, flags = _s_value(value)
        try:
            super(Client, self).add(key, value, expiration=time, flags=flags)
            return True
        except omcache.KeyExistsError:
            return False

    def cas(self, key, value, cas, time=0):
        value, flags = _s_value(value)
        try:
            super(Client, self).set(key, value, expiration=time, cas=cas, flags=flags)
            return True
        except omcache.KeyExistsError:
            return False

    def replace(self, key, value, time=0):
        value, flags = _s_value(value)
        try:
            super(Client, self).replace(key, value, expiration=time, flags=flags)
            return True
        except omcache.NotFoundError:
            return False

    def append(self, key, value):
        value, _ = _s_value(value)  # ignore flags
        try:
            super(Client, self).append(key, value)
            return True
        except omcache.NotStoredError:
            return False

    def prepend(self, key, value):
        value, _ = _s_value(value)  # ignore flags
        try:
            super(Client, self).prepend(key, value)
            return True
        except omcache.NotStoredError:
            return False

    def set_multi(self, mapping, time=0, key_prefix=None):
        # pylibmc's set_multi returns a list of failed keys, but we don't
        # have such an operation at the moment without blocking or using
        # response callbacks
        # XXX: handle failed sets
        failed = []
        for key, value in mapping.items():
            try:
                prefixed_key = "{0}{1}".format(key_prefix or "", key)
                value, flags = _s_value(value)
                super(Client, self).set(prefixed_key, value, flags=flags,
                                        expiration=time, timeout=0)
            except omcache.CommandError:
                failed.append(key)
        return failed

    def delete(self, key):
        try:
            super(Client, self).delete(key)
            return True
        except omcache.NotFoundError:
            return False

    def delete_multi(self, keys, time=0, key_prefix=None):
        # pylibmc's delete_multi returns False if all keys weren't
        # successfully deleted (for example if they didn't exist at all),
        # but we don't have such an operation at the moment without blocking
        # or using response callbacks
        # XXX: handle failed deletes
        # NOTE: time argument is not supported by omcache
        success = True
        for key in keys:
            try:
                prefixed_key = "{0}{1}".format(key_prefix or "", key)
                super(Client, self).delete(prefixed_key, timeout=0)
            except omcache.CommandError:
                success = False
        return success
