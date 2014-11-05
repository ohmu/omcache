/*
 * OMcache: distribution functions
 *
 * Copyright (c) 2014, Oskari Saarenmaa <os@ohmu.fi>
 * All rights reserved.
 *
 * This file is under the Apache License, Version 2.0.
 * See the file `LICENSE` for details.
 *
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "omcache_priv.h"


omc_hidden uint32_t omc_hash_jenkins_oat(const unsigned char *key, size_t key_len)
{
  // http://en.wikipedia.org/wiki/Jenkins_hash_function#one-at-a-time
  uint32_t hash, i;
  for (hash = i = 0; i < key_len; i ++)
    {
      hash += key[i];
      hash += (hash << 10);
      hash ^= (hash >> 6);
    }
  hash += (hash << 3);
  hash ^= (hash >> 11);
  hash += (hash << 15);
  return hash;
}

static uint32_t omc_hash_md5_32(const unsigned char *key, size_t key_len)
{
  // truncate md5 hash to 32 bits like libmemcached's hashkit
  unsigned char md5buf[16];
  omc_hash_md5((unsigned char *) key, key_len, md5buf);
  return ((uint32_t) md5buf[3] << 24) | ((uint32_t) md5buf[2] << 16) |
         ((uint32_t) md5buf[1] <<  8) | ((uint32_t) md5buf[0] <<  0);
}

static size_t omc_ketama_point_name(const char *hostname, const char *portname, uint32_t point, char *namebuf)
{
  // libmemcached ketama appends port number to hostname if it's not the default (11211)
  bool with_port = strcmp(portname, MC_PORT) != 0;
  return sprintf(namebuf, "%s%s%s-%u",
    hostname, with_port ? ":" : "", with_port ? portname : "", point);
}

static uint32_t omc_ketama_jenkins_oat(const char *hostname, const char *portname,
                                           uint32_t point, uint32_t *hashes)
{
  char name[strlen(hostname) + strlen(portname) + 16];
  size_t name_len = omc_ketama_point_name(hostname, portname, point, name);
  hashes[0] = omc_hash_jenkins_oat((unsigned char *) name, name_len);
  return 1;
}

static uint32_t omc_ketama_md5_libmcd_weighted(const char *hostname, const char *portname,
                                                   uint32_t point, uint32_t *hashes)
{
  // libmemcached ketama grabs four different hash values from a single md5
  // buffer in 'weighted ketama' mode
  char name[strlen(hostname) + strlen(portname) + 16];
  size_t name_len = omc_ketama_point_name(hostname, portname, point, name);
  unsigned char md5buf[16];
  omc_hash_md5((unsigned char *) name, name_len, md5buf);
  for (int offset = 0; offset < 4; offset ++)
    hashes[offset] =
      ((uint32_t) md5buf[3 + offset * 4] << 24) |
      ((uint32_t) md5buf[2 + offset * 4] << 16) |
      ((uint32_t) md5buf[1 + offset * 4] <<  8) |
      ((uint32_t) md5buf[0 + offset * 4] <<  0);
  return 4;
}

omcache_dist_t omcache_dist_libmemcached_ketama = {
  .omcache_version = OMCACHE_VERSION,
  .points_per_server = 100,
  .entries_per_point = 1,
  .point_hash_func = omc_ketama_jenkins_oat,
  .key_hash_func = omc_hash_jenkins_oat,
  };

omcache_dist_t omcache_dist_libmemcached_ketama_weighted = {
  .omcache_version = OMCACHE_VERSION,
  .points_per_server = 40,
  .entries_per_point = 4,
  .point_hash_func = omc_ketama_md5_libmcd_weighted,
  .key_hash_func = omc_hash_md5_32,
  };

omcache_dist_t omcache_dist_libmemcached_ketama_pre1010 = {
  .omcache_version = OMCACHE_VERSION,
  .points_per_server = 40,
  .entries_per_point = 4,
  .point_hash_func = omc_ketama_md5_libmcd_weighted,
  .key_hash_func = omc_hash_jenkins_oat,
  };
