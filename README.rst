OMcache
=======

OMcache is a low level C library for accessing memcached servers.  The goals
of the OMcache project are stable API and ABI and 'easy' integration into
complex applications and systems; OMcache specifically does not mask any
signals or call any blocking functions.  [FIXME: this is not really true at
the moment, OMcache uses getaddrinfo for name resolution which blocks, pass
IP addresses for OMcache to avoid blocking for now.]

**NOTE** OMcache is still very much work-in-progress and interfaces can
change without notice and the git tree may be rebased so don't rely on this
just yet.

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

Contact
=======

OMcache is maintained by Oskari Saarenmaa <os@ohmu.fi>, bug reports and
patches are very welcome, please post them as GitHub issues and pull
requests at https://github.com/saaros/omcache
