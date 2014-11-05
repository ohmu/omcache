OMcache
=======

OMcache is a C and Python library for accessing memcached servers.  The
goals of the OMcache project are a stable API and ABI and 'easy' integration
into complex applications and systems.  OMcache is meant to be used as a
middleware layer to provide redundancy and consistency to a memcached server
cluster.  OMcache specifically tries to avoid ABI brekage and does not mask
any signals or call blocking functions to help integrating it into other
solutions.

FIXME: Currently OMcache uses getaddrinfo for name resolution which blocks,
pass IP addresses for OMcache to avoid blocking for now.

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


Compatibility
=============

OMcache tries to be compatible with libmemcached where possible.  During
normal operations where all servers are available and no failovers have
happened the two memcache clients should always select the same servers for
keys.  Libmemcached's failover mechanism has traditionally been poorly
documented and its details have changed occasionally between the releases
so 100% compatibility is not possible.

The default KETAMA implementation was changed in libmemcached 1.0.10.
OMcache defaults to the new algorithm but also has support for the older
algorithm, the caller must select the older algorithm if compatibility with
older libmemcached versions is required.

OMcache also provides a thin API compatibility wrapper header which allows
simple applications to be converted to use OMcache instead of libmemcached
by including "omcache_libmemcached.h" instead of "libmemcached/memcached.h".
Similarly there's a compatibility layer for pylibmc in omcache_pylibmc.py
which provides a limited pylibmc-like API for OMcache.

The functionality provided by these wrappers is limited and they are not
supported, they're provided to make it easy to test OMcache in simple
programs that use the libmemcached or pylibmc API.

License
=======

OMcache is released under the Apache License, Version 2.0.

For the exact license terms, see `LICENSE` and
http://opensource.org/licenses/Apache-2.0 .

Credits
=======

OMcache was created and is maintained by Oskari Saarenmaa <os@ohmu.fi>.

F-Secure Corporation provided the infrastructure for testing OMcache in its
initial development process and contributed to the pylibmc and libmemcached
compatibility layers as well as ported pgmemcache (PostgreSQL memcached
interface) to run on OMcache.

MD5 implementation is based on Solar Designer's Public Domain / BSD licensed
code from openwall.info.

Contact
=======

Bug reports and patches are very welcome, please post them as GitHub issues
and pull requests at https://github.com/saaros/omcache
