import logging
import os
import random
import subprocess
import time
import unittest


logging.basicConfig(level=logging.INFO)


class OMcacheCase(unittest.TestCase):
    a_memcacheds = []
    c_memcacheds = []
    m_mc_index = None
    log = None

    def setup_method(self, method):
        self.m_mc_index = 0
        self.log = logging.getLogger(method.__name__)

    @classmethod
    def teardown_class(cls):
        for proc in cls.a_memcacheds:
            proc.kill()

    @classmethod
    def start_memcached(cls, addr=None):
        memcached_path = os.getenv("MEMCACHED_PATH") or "/usr/bin/memcached"
        if not addr:
            addr = "127.0.0.1"
        port = random.randrange(30000,60000)
        proc = subprocess.Popen([memcached_path, "-vp", str(port), "-l", addr],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        time.sleep(0.1)
        assert proc.poll() is None
        cls.a_memcacheds.append(proc)
        if addr is None:
            cls.c_memcacheds.append(proc)
        return "{0}:{1}".format(addr, port)

    def get_memcached(self):
        try:
            return self.c_memcacheds[self.m_mc_index]
        except IndexError:
            return self.start_memcached()
        finally:
            self.m_mc_index += 1
