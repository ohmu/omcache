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

#ifndef _OMCACHE_PRIV_H
#define _OMCACHE_PRIV_H 1

#include "omcache.h"
#include "memcached_protocol_binary.h"

#define MC_PORT "11211"

#define omc_hidden __attribute__((visibility("hidden")))

omc_hidden void omc_hash_md5(const unsigned char *key, size_t key_len, unsigned char *buf);
omc_hidden uint32_t omc_hash_jenkins_oat(const unsigned char *key, size_t key_len);

#endif // !_OMCACHE_PRIV_H
