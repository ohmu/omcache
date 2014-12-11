OMcache |BuildStatus|_ |CoverityStatus|_
========================================

.. |CoverityStatus| image:: https://scan.coverity.com/projects/3408/badge.svg
.. _CoverityStatus: https://scan.coverity.com/projects/3408/
.. |BuildStatus| image:: https://travis-ci.org/ohmu/omcache.png?branch=master
.. _BuildStatus: https://travis-ci.org/ohmu/omcache

OMcache is a C and Python library for accessing memcached servers.  The
goals of the OMcache project are a stable API and ABI and 'easy' integration
into complex applications and systems.  OMcache is meant to be used as a
middleware layer to provide redundancy and consistency to a memcached server
cluster.  OMcache specifically tries to avoid ABI brekage and does not mask
any signals or call blocking functions to help integrating it into other
solutions.

::

                                           ,##,
              OMcache                    ;@@@@@;
                                ;#,        `@@'       ;@@,
            ,,....,               @@,                `@@@@@`
        ,'#@@@@@@@@@#;`           `#@@@#;          .@@@@@@@`
        #@@@@@@@@@@@@@@@@,            `#@@@#+++#@@@@@@@@@@.
          `@@'           @@#             `#@@@@@@@@@@@@#.
                        ;@@@@,
                     .#@@@@@@@
              ``.;#@@@@@@@@@'          .:'#@@#;;,
               '@@@@@@@@@@@@@     ,#@@@@@@@@@@@@@@@@'
                 `@@@'    `@@@##@@@@@@@@@@@',`    `#@@;,
                              @@@@@@@@+.             `#@@#
         ,##;,               :@@@@@;            '#@@@@@@@@@`
           ,@@#;,          `#@@@@@+           `@@@@@@@@@@@#
               `@@@@@@@@@@@@@@@@@@             #@@@@'
                   `#@@@@@@@@@@#'                `@'


Platform requirements
=====================

OMcache has been developed and tested on x86-64 Linux, it will probably
require some changes to work on other platforms.

OMcache can be built with recent versions of GCC and Clang.  Clang and GCC
versions prior to 4.9 will emit some `spurious warnings`_ about missing
field initializers.  Asynchronous name lookups require the libasyncns_
library; OMcache can be built without it by passing WITHOUT_ASYNCNS=1
argument to make, but that will cause name lookups to become blocking
operations.

Unit tests are implemented using the Check_ unit testing framework.  Check
version 0.9.10 or newer is recommended, earlier versions can be used but
their log output is limited.

The Python module requires CFFI_ 0.6+ and supports CPython_ 2.6, 2.7 and
3.3+ and PyPy_ 2.2+.

.. _`spurious warnings`: https://github.com/ohmu/omcache/issues/11
.. _libasyncns: http://0pointer.de/lennart/projects/libasyncns/
.. _Check: http://check.sourceforge.net/
.. _CFFI: https://cffi.readthedocs.org/
.. _CPython: https://www.python.org/
.. _PyPy: http://pypy.org/

Compatibility
=============

OMcache tries to be compatible with libmemcached_ where possible.  During
normal operations where all servers are available and no failovers have
happened the two memcache clients should always select the same servers for
keys.  Libmemcached's failover mechanism has traditionally been poorly
documented and its details have changed occasionally between the releases
so 100% compatibility is not possible.

OMcache supports configurable `key distribution algorithms`_, the default is
the one used by libmemcached version 1.0.10 and newer, called
``omcache_dist_libmemcached_ketama`` in OMcache.  Libmemcached versions
prior to 1.0.10 had a bug in the implementation of the algorithm which
caused it to use a mix of the normal ketama and weighted ketama, using
Jenkins hashing for keys and md5 hashing for hosts.  OMcache supports this
algorithm as ``omcache_dist_libmemcached_ketama_pre1010``.

OMcache also provides a thin API compatibility wrapper header which allows
simple applications to be converted to use OMcache instead of libmemcached
by including "omcache_libmemcached.h" instead of "libmemcached/memcached.h".
Similarly there's a compatibility layer for pylibmc_ in omcache_pylibmc.py
which provides a limited pylibmc-like API for OMcache.

The functionality provided by these wrappers is limited and they are not
supported, they're provided to make it easy to test OMcache in simple
programs that use the libmemcached and pylibmc APIs.

.. _`key distribution algorithms`: http://en.wikipedia.org/wiki/Consistent_hashing
.. _libmemcached: http://libmemcached.org/
.. _pylibmc: http://sendapatch.se/projects/pylibmc/

License
=======

OMcache is released under the Apache License, Version 2.0.

For the exact license terms, see `LICENSE` and
http://opensource.org/licenses/Apache-2.0 .

Credits
=======

OMcache was created by Oskari Saarenmaa <os@ohmu.fi> and is maintained by
Ohmu Ltd's hackers <opensource@ohmu.fi>.

F-Secure Corporation provided the infrastructure for testing OMcache in its
initial development process and contributed to the pylibmc and libmemcached
compatibility layers as well as ported pgmemcache (PostgreSQL memcached
interface) to run on OMcache.

MD5 implementation is based on Solar Designer's Public Domain / BSD licensed
code from openwall.info.

Contact
=======

Bug reports and patches are very welcome, please post them as GitHub issues
and pull requests at https://github.com/ohmu/omcache
