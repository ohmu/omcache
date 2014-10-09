# omcache_pylibmc.py - a kludgy pylbmc API compatibility layer
#
# Written by Oskari Saarenmaa <os@ohmu.fi>, and is placed in the public
# domain.  The author hereby disclaims copyright to this source code.

import omcache
import warnings

NotFound = omcache.NotFoundError


class Client(omcache.OMcache):
    def __init__(self, servers, behaviors=None, binary=None, username=None, password=None):
        if username or password:
            raise omcache.Error("OMcache does not support authentication at the moment")
        if binary is False:
            warnings.warn("OMcache always uses binary protocol, ignoring binary=False")
        super(Client, self).__init__(servers)
        if behaviors:
            for k, v in behaviors.items():
                if k == "ketama":
                    if not v:
                        warnings.warn("OMcache always uses ketama")
                else:
                    warnings.warn("OMcache does not support behavior {!r}".format(k))

    incr = omcache.OMcache.increment
    decr = omcache.OMcache.decrement

    def cas(self, key, val, cas, time=0):
        return self.set(key, val, expiration=time, cas=cas)

    def get(self, key, cas=False):
        try:
            return super(Client, self).get(key, cas)
        except NotFound:
            return None

    def gets(self, key):
        return self.get(key, cas=True)

    def get_multi(self, keys, key_prefix=None):
        # omcache doesn't provide get_multi like operation without using
        # response callbacks at the moment.
        # XXX: do something different, don't block for each key
        result = {}
        for key in keys:
            try:
                value = self.get("{}{}".format(key_prefix or "", key))
            except NotFound:
                continue
            result[key] = value
        return result

    def set_multi(self, mapping, time=0, key_prefix=None):
        # pylibmc's set_multi returns a list of failed keys, but we don't
        # have such an operation at the moment without blocking or using
        # response callbacks
        # XXX: handle failed sets
        failed = []
        for key, val in mapping.items():
            try:
                self.set("{}{}".format(key_prefix or "", key), val, expiration=time, timeout=0)
            except omcache.CommandError as ex:
                failed.append(key)
        return failed

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
                self.delete("{}{}".format(key_prefix or "", key), timeout=0)
            except omcache.CommandError as ex:
                success = False
        return success
