/*
 * OMcache: internal utility functions
 *
 * Copyright (c) 2014, Oskari Saarenmaa <os@ohmu.fi>
 * All rights reserved.
 *
 * This file is under the Apache License, Version 2.0.
 * See the file `LICENSE` for details.
 *
 */

#include <stdlib.h>
#include <string.h>
#include "omcache_priv.h"

// murmurhash3's finalization function
static inline uint32_t
omc_hash_uint32(uint32_t val)
{
  val ^= val >> 16;
  val *= 0x85ebca6b;
  val ^= val >> 13;
  val *= 0xc2b2ae35;
  val ^= val >> 16;
  return val;
}

static void omc_int_hash_table_reset(omc_int_hash_table_t *hash)
{
  memset(hash->buckets, 0, hash->size * sizeof(omc_int_hash_node_t *));
  hash->freelist = NULL;
  for (uint32_t i = 0; i < hash->size; i ++)
    {
      hash->nodes[i].key = -1;
      hash->nodes[i].val = -1;
      hash->nodes[i].next = hash->freelist;
      hash->freelist = &hash->nodes[i];
    }
}

omc_int_hash_table_t *omc_int_hash_table_init(omc_int_hash_table_t *old_hash, uint32_t size)
{
  // reuse old hash table if one was given and the size is right
  if (old_hash != NULL)
    {
      if (old_hash->size >= size && old_hash->size < size * 2)
        {
          omc_int_hash_table_reset(old_hash);
          return old_hash;
        }
      omc_int_hash_table_free(old_hash);
    }
  // allocate the hash table and enough memory to hold all the nodes and
  // pointers to bucket heads in a single allocation
  size_t struct_alloc = sizeof(omc_int_hash_table_t);
  size_t nodes_alloc = size * sizeof(omc_int_hash_node_t);
  size_t pointers_alloc = size * sizeof(omc_int_hash_node_t *);
  omc_int_hash_table_t *hash = malloc(struct_alloc + nodes_alloc + pointers_alloc);
  hash->size = size;
  hash->buckets = (omc_int_hash_node_t **) (
    ((unsigned char *) hash) + struct_alloc + nodes_alloc);
  omc_int_hash_table_reset(hash);
  return hash;
}

void omc_int_hash_table_free(omc_int_hash_table_t *hash)
{
  free(hash);
}

int omc_int_hash_table_find(omc_int_hash_table_t *hash, int key)
{
  uint32_t bucket = omc_hash_uint32(key) % hash->size;
  for (omc_int_hash_node_t *n = hash->buckets[bucket]; n != NULL; n = n->next)
    if (n->key == key)
      return n->val;
  return -1;
}

int omc_int_hash_table_add(omc_int_hash_table_t *hash, int key, int val)
{
  uint32_t bucket = omc_hash_uint32(key) % hash->size;
  omc_int_hash_node_t *n = hash->freelist;
  hash->freelist = n->next;
  n->next = hash->buckets[bucket];
  n->key = key;
  n->val = val;
  hash->buckets[bucket] = n;
  return 0;
}

int omc_int_hash_table_del(omc_int_hash_table_t *hash, int key)
{
  uint32_t bucket = omc_hash_uint32(key) % hash->size;
  omc_int_hash_node_t *p = NULL;
  for (omc_int_hash_node_t *n = hash->buckets[bucket]; n != NULL; n = n->next)
    {
      if (n->key == key)
        {
          if (p)
            p->next = n->next;
          else
            hash->buckets[bucket] = n->next;
          n->next = hash->freelist;
          n->key = -1;
          n->val = -1;
          hash->freelist = n;
          return 0;
        }
      p = n;
    }
  return -1;
}
