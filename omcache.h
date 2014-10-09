/*
 * OMcache - a memcached client library
 *
 * Copyright (c) 2013-2014, Oskari Saarenmaa <os@ohmu.fi>
 * All rights reserved.
 *
 * This file is under the Apache License, Version 2.0.
 * See the file `LICENSE` for details.
 *
 */

#ifndef _OMCACHE_H
#define OMCACHE_H 1

#include <poll.h>
#include <stdarg.h>
#include <stdint.h>

#include "omcache_cdef.h"

// CFFI can't handle __attribute__ so redefine omcache_log_stderr here
void omcache_log_stderr(void *context, int level, const char *fmt, ...)
        __attribute__((format (printf, 3, 4)));

#endif // !_OMCACHE_H
