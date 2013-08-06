/*
 * OMcache - a memcached client library
 *
 * Copyright (c) 2013, Oskari Saarenmaa <os@ohmu.fi>
 * All rights reserved.
 *
 * This file is under the 2-clause BSD license.
 * See the file `LICENSE` for details.
 *
 */

#ifndef _OMCACHE_H
#define OMCACHE_H 1

#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/uio.h>

#include "omcache_cdef.h"

#define OMCACHE_DIST_KETAMA(mc) omcache_set_dist_func((mc), \
                                omcache_dist_ketama_init, \
                                omcache_dist_ketama_free, \
                                omcache_dist_ketama_lookup, (mc))
#define OMCACHE_DIST_MODULO(mc) omcache_set_dist_func((mc), NULL, NULL, \
                                omcache_dist_modulo_lookup, (mc))

// CFFI can't handle __attribute__ so redefine omcache_log_stderr here
void omcache_log_stderr(void *context, int level, const char *fmt, ...)
        __attribute__((format (printf, 3, 4)));
#define OMCACHE_LOG_STDERR(mc) omcache_set_log_func((mc), omcache_log_stderr, NULL)

#endif // !_OMCACHE_H
