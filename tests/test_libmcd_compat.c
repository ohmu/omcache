/*
 * libmemcached compatibility test
 *
 * Copyright (c) 2014, Oskari Saarenmaa <os@ohmu.fi>
 * All rights reserved.
 *
 * This file is under the Apache License, Version 2.0.
 * See the file `LICENSE` for details.
 *
 */

#include "test_omcache.h"

#ifdef WITH_LIBMEMCACHED
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <libmemcached/memcached.h>

#define ck_libmcd(c,e) cd_omcache((c), (e))
#define ck_libmcd_ok(c) ck_omcache((c), MEMCACHED_SUCCESS)

static void set_n_values(omcache_t *oc, memcached_st *mc,
                         char **keys_omc, char **keys_mcd,
                         size_t *key_lens_omc, size_t *key_lens_mcd,
                         char **keys_all, size_t *key_lens_all,
                         size_t key_count)
{
  ck_omcache_ok(omcache_set_buffering(oc, true));
  for (size_t i = 0; i < key_count; i ++)
    {
      key_lens_omc[i] = asprintf(&keys_omc[i], "test_ketama_omc_%zu", i);
      ck_omcache(OMCACHE_BUFFERED,
        omcache_set(oc, (cuc *) keys_omc[i], key_lens_omc[i], (cuc *) keys_omc[i], key_lens_omc[i], 0, 0, 0, 0));
      key_lens_mcd[i] = asprintf(&keys_mcd[i], "test_ketama_mcd_%zu", i);
      ck_libmcd_ok(memcached_set(mc, keys_mcd[i], key_lens_mcd[i], keys_mcd[i], key_lens_mcd[i], 0, 0));
      keys_all[2 * i + 0] = keys_omc[i];
      keys_all[2 * i + 1] = keys_mcd[i];
      key_lens_all[2 * i + 0] = key_lens_omc[i];
      key_lens_all[2 * i + 1] = key_lens_mcd[i];
    }
  ck_omcache_ok(omcache_set_buffering(oc, false));
  ck_omcache_ok(omcache_io(oc, NULL, NULL, NULL, NULL, 5000));
}

static void check_n_values(omcache_t *oc, memcached_st *mc,
                           char **keys, size_t *key_lens,
                           size_t *omc_found, size_t *mcd_found,
                           size_t key_count)
{
  // check omcache
  omcache_value_t values[key_count];
  size_t value_count = key_count;
  omcache_req_t reqs[key_count];
  size_t req_count = key_count;
  ck_omcache_ok_or_again(omcache_get_multi(oc, (cuc **) keys, key_lens, key_count, reqs, &req_count, values, &value_count, 5000));
  size_t omcache_values_found = value_count;
  while (req_count > 0)
    {
      int ret = OMCACHE_AGAIN;
      while (ret == OMCACHE_AGAIN)
        {
          value_count = key_count;
          ret = omcache_io(oc, reqs, &req_count, values, &value_count, 5000);
          ck_omcache_ok_or_again(ret);
          omcache_values_found += value_count;
        }
    }
  if (omc_found)
    *omc_found = omcache_values_found;
  else
    ck_assert_int_eq(omcache_values_found, key_count);

  // check libmemcached
  size_t libmemcached_values_found = 0;
  for (size_t i = 0; i < key_count; i ++)
    {
      memcached_return_t ret;
      char *value = memcached_get(mc, keys[i], key_lens[i], NULL, NULL, &ret);
      if (ret == MEMCACHED_SUCCESS)
        {
          libmemcached_values_found ++;
          free(value);
        }
    }
  if (mcd_found)
    *mcd_found = libmemcached_values_found;
  else
    ck_assert_int_eq(libmemcached_values_found, key_count);
}

static void test_libmemcached_ketama_compatibility_m(omcache_dist_t *omcache_method,
                                                     memcached_behavior_t libmcd_method)
{
  omcache_t *oc = ot_init_omcache(0, LOG_INFO);
  memcached_st *mc = memcached_create(NULL);
  char *keys_omc[1000], *keys_mcd[1000], *keys_all[2000];
  size_t key_lens_omc[1000], key_lens_mcd[1000], key_lens_all[2000];

  pid_t mc_pid0;  // mc0 is our private memcached that we can break
  int mc_port0 = ot_start_memcached(NULL, &mc_pid0);
  int mc_port1 = ot_get_memcached(0);
  int mc_port2 = ot_get_memcached(1);

  char srvbuf[200];
  sprintf(srvbuf, "127.0.0.1:%d,127.0.0.1:%d,127.0.0.1:%d", mc_port0, mc_port1, mc_port2);
  ck_omcache_ok(omcache_set_servers(oc, srvbuf));
  ck_omcache_ok(omcache_set_dead_timeout(oc, 4000));
  ck_omcache_ok(omcache_set_reconnect_timeout(oc, 3000));
  ck_omcache_ok(omcache_set_distribution_method(oc, omcache_method));

  ck_libmcd_ok(memcached_behavior_set(mc, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL, 1));
  ck_libmcd_ok(memcached_behavior_set(mc, libmcd_method, 1));
  ck_libmcd_ok(memcached_behavior_set(mc, MEMCACHED_BEHAVIOR_DEAD_TIMEOUT, 4));
  ck_libmcd_ok(memcached_behavior_set(mc, MEMCACHED_BEHAVIOR_RETRY_TIMEOUT, 3));
  ck_libmcd_ok(memcached_server_add(mc, "127.0.0.1", mc_port0));
  ck_libmcd_ok(memcached_server_add(mc, "127.0.0.1", mc_port1));
  ck_libmcd_ok(memcached_server_add(mc, "127.0.0.1", mc_port2));

  // set 1000 keys in memcached servers using omcache and libmemcached
  set_n_values(oc, mc, keys_omc, keys_mcd, key_lens_omc, key_lens_mcd, keys_all, key_lens_all, 1000);

  // verify that both clients can read all values set by both clients
  check_n_values(oc, mc, keys_omc, key_lens_omc, NULL, NULL, 1000);
  check_n_values(oc, mc, keys_mcd, key_lens_mcd, NULL, NULL, 1000);

  for (int i = 0; i < 2000; i ++)
    free(keys_all[i]);

  omcache_free(oc);
  memcached_free(mc);
}

START_TEST(test_ketama_compatibility)
{
  // libmemcached doesn't provide a numeric version of the library, parse it
  // here to try to figure out which distribution algorithm it's using
  omcache_dist_t *dist = &omcache_dist_libmemcached_ketama;
  int v = strncmp(memcached_lib_version(), "1.0.", 4);
  if (v < 0 || (v == 0 && atoi(memcached_lib_version() + 4) < 10))
    dist = &omcache_dist_libmemcached_ketama_pre1010;
  test_libmemcached_ketama_compatibility_m(dist, MEMCACHED_BEHAVIOR_KETAMA);
}
END_TEST

START_TEST(test_ketama_weighted_compatibility)
{
  test_libmemcached_ketama_compatibility_m(
    &omcache_dist_libmemcached_ketama_weighted, MEMCACHED_BEHAVIOR_KETAMA_WEIGHTED);
}
END_TEST

Suite *ot_suite_libmcd_compat(void)
{
  Suite *s = suite_create("libmemcached compat");
  tcase_set_timeout(ot_tcase_add(s, test_ketama_compatibility), 60);
  tcase_set_timeout(ot_tcase_add(s, test_ketama_weighted_compatibility), 60);
  return s;
}
#endif // WITH_LIBMEMCACHED
