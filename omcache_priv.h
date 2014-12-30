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
#include "compat.h"

#define MC_PORT "11211"

#define omc_hidden __attribute__((visibility("hidden")))

typedef struct omc_hash_node_s
{
  uint32_t key;
  void *val;
  struct omc_hash_node_s *next;
} omc_hash_node_t;

typedef struct omc_hash_table_s
{
  uint32_t size;
  uint32_t count;
  void *not_found_val;
  omc_hash_node_t **buckets;
  omc_hash_node_t *freelist;
  omc_hash_node_t nodes[];
} omc_hash_table_t;

omc_hidden omc_hash_table_t *omc_hash_table_init(omc_hash_table_t *hash, uint32_t size, void *not_found_val);
omc_hidden void omc_hash_table_free(omc_hash_table_t *hash);
omc_hidden void *omc_hash_table_find(omc_hash_table_t *hash, uint32_t key);
omc_hidden int omc_hash_table_add(omc_hash_table_t *hash, uint32_t key, void *val);
omc_hidden void *omc_hash_table_del(omc_hash_table_t *hash, uint32_t key);

#define omc_int_hash_table_t omc_hash_table_t
#define omc_int_hash_table_init(h,s) omc_hash_table_init((h), (s), (void *) (uintptr_t) -1)
#define omc_int_hash_table_free(h) omc_hash_table_free(h)
#define omc_int_hash_table_find(h,k) ((intptr_t) omc_hash_table_find((h), (k)))
#define omc_int_hash_table_add(h,k,v) omc_hash_table_add((h), (k), (void *) (uintptr_t) (v))
#define omc_int_hash_table_del(h,k) omc_hash_table_del((h), (k))

omc_hidden void omc_hash_md5(const unsigned char *key, size_t key_len, unsigned char *buf);
omc_hidden uint32_t omc_hash_jenkins_oat(const unsigned char *key, size_t key_len);

#endif // !_OMCACHE_PRIV_H
