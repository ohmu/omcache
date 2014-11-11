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

typedef struct omc_int_hash_node_s
{
  int key;
  int val;
  struct omc_int_hash_node_s *next;
} omc_int_hash_node_t;

typedef struct omc_int_hash_table_s
{
  uint32_t size;
  omc_int_hash_node_t **buckets;
  omc_int_hash_node_t *freelist;
  omc_int_hash_node_t nodes[];
} omc_int_hash_table_t;

omc_hidden omc_int_hash_table_t *omc_int_hash_table_init(omc_int_hash_table_t *hash, uint32_t size);
omc_hidden void omc_int_hash_table_free(omc_int_hash_table_t *hash);
omc_hidden int omc_int_hash_table_find(omc_int_hash_table_t *hash, int key);
omc_hidden int omc_int_hash_table_add(omc_int_hash_table_t *hash, int key, int val);
omc_hidden int omc_int_hash_table_del(omc_int_hash_table_t *hash, int key);

omc_hidden void omc_hash_md5(const unsigned char *key, size_t key_len, unsigned char *buf);
omc_hidden uint32_t omc_hash_jenkins_oat(const unsigned char *key, size_t key_len);

#endif // !_OMCACHE_PRIV_H
