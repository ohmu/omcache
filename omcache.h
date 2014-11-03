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

#include <stdint.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include "omcache_cdef.h"

#ifdef __cplusplus
}
#endif // __cplusplus

// CFFI can't handle defines yet
#define OMCACHE_VERSION 0x00000100  // Version 0.1.0
#define OMCACHE_DELTA_NO_ADD 0xffffffffu

#endif // !_OMCACHE_H
