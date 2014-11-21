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

static omc_hash_table_t *omc_hash_table_reset(omc_hash_table_t *hash)
{
  hash->count = 0;
  hash->freelist = NULL;
  for (uint32_t i = 0; i < hash->size; i ++)
    {
      hash->buckets[i] = NULL;
      hash->nodes[i].key = -1;
      hash->nodes[i].val = hash->not_found_val;
      hash->nodes[i].next = hash->freelist;
      hash->freelist = &hash->nodes[i];
    }
  return hash;
}

omc_hash_table_t *omc_hash_table_init(omc_hash_table_t *old_hash, uint32_t size, void *not_found_val)
{
  // reuse old hash table if one was given and the size is right
  if (old_hash != NULL)
    {
      if (old_hash->size >= size)
        return omc_hash_table_reset(old_hash);
      omc_hash_table_free(old_hash);
    }
  // allocate the hash table and enough memory to hold all the nodes and
  // pointers to bucket heads in a single allocation
  size_t struct_alloc = sizeof(omc_hash_table_t);
  size_t nodes_alloc = size * sizeof(omc_hash_node_t);
  size_t pointers_alloc = size * sizeof(omc_hash_node_t *);
  omc_hash_table_t *hash = malloc(struct_alloc + nodes_alloc + pointers_alloc);
  hash->not_found_val = not_found_val;
  hash->size = size;
  hash->buckets = (omc_hash_node_t **) (
    ((unsigned char *) hash) + struct_alloc + nodes_alloc);
  return omc_hash_table_reset(hash);
}

void omc_hash_table_free(omc_hash_table_t *hash)
{
  free(hash);
}

void *omc_hash_table_find(omc_hash_table_t *hash, uint32_t key)
{
  uint32_t bucket = omc_hash_uint32(key) % hash->size;
  for (omc_hash_node_t *n = hash->buckets[bucket]; n != NULL; n = n->next)
    if (n->key == key)
      return n->val;
  return hash->not_found_val;
}

int omc_hash_table_add(omc_hash_table_t *hash, uint32_t key, void *val)
{
  if (hash->count == hash->size)
    return -1;
  uint32_t bucket = omc_hash_uint32(key) % hash->size;
  omc_hash_node_t *n = hash->freelist;
  hash->freelist = n->next;
  n->next = hash->buckets[bucket];
  n->key = key;
  n->val = val;
  hash->buckets[bucket] = n;
  hash->count ++;
  return 0;
}

void *omc_hash_table_del(omc_hash_table_t *hash, uint32_t key)
{
  uint32_t bucket = omc_hash_uint32(key) % hash->size;
  omc_hash_node_t *p = NULL;
  for (omc_hash_node_t *n = hash->buckets[bucket]; n != NULL; n = n->next)
    {
      if (n->key == key)
        {
          void *val = n->val;
          if (p)
            p->next = n->next;
          else
            hash->buckets[bucket] = n->next;
          n->key = -1;
          n->val = hash->not_found_val;
          n->next = hash->freelist;
          hash->freelist = n;
          hash->count --;
          return val;
        }
      p = n;
    }
  return hash->not_found_val;
}
