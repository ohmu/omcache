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

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "omcache_cdef.h"

// CFFI can't handle defines yet
#define OMCACHE_DELTA_NO_ADD 0xffffffffu

#endif // !_OMCACHE_H
