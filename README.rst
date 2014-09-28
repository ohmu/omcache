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
documented and its details have changed occassionally between the releases
so 100% compatibility is not possible.

OMcache also provides a thin API compatibility wrapper header which allows
simple applications to be converted to use OMcache instead of libmemcached
by just including "omcache_libmemcached.h" insetad of
"libmemcached/memcached.h".  The functionality provided by this wrapper is
limited and it is not supported, it's provided to make it easy to test
OMcache in simple programs that use the libmemcached API.

License
=======

OMcache is released under a 2-clause BSD license.  For the exact license
terms, see `LICENSE` and http://opensource.org/licenses/BSD-2-Claused .

Contact
=======

OMcache is maintained by Oskari Saarenmaa <os@ohmu.fi>, bug reports and
patches are very welcome, please post them as GitHub issues and pull
requests at https://github.com/saaros/omcache
