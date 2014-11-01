/*
 * Unit tests for OMcache
 *
 * Copyright (c) 2014, Oskari Saarenmaa <os@ohmu.fi>
 * All rights reserved.
 *
 * This file is under the Apache License, Version 2.0.
 * See the file `LICENSE` for details.
 *
 */

#include "test_omcache.h"

#define TIMEOUT 2000

START_TEST(test_get)
{
  unsigned char *val;
  const unsigned char *get_val;
  size_t val_len, get_val_len;
  uint32_t flags;
  uint64_t cas;
  int mc_port1, mc_port2;
  char srvstr[100];

  ot_init_omcache(oc, LOG_DEBUG);
  mc_port1 = ot_start_memcached(NULL);
  mc_port2 = ot_start_memcached(NULL);
  snprintf(srvstr, sizeof(srvstr), "127.0.0.1:%d, 127.0.0.1:%d", mc_port1, mc_port2);
  ck_omcache_ok(omcache_set_servers(oc, srvstr));

  ck_omcache(omcache_get(oc, (cuc *) "foo", 3, NULL, NULL, NULL, NULL, TIMEOUT), OMCACHE_NOT_FOUND);
  ck_omcache(omcache_set(oc, (cuc *) "foo", 3, (cuc *) "bar", 3, 0, 42, 0, TIMEOUT), OMCACHE_OK);

  ck_omcache(omcache_get(oc, (cuc *) "foo", 3, &get_val, &val_len, &flags, &cas, TIMEOUT), OMCACHE_OK);
  ck_assert_uint_eq(val_len, 3);
  ck_assert_int_eq(memcmp(get_val, "bar", 3), 0);
  ck_assert_uint_eq(flags, 42);
  ck_assert(cas != 0);

  // memcached allows 1mb values by default
  val_len = 2048 * 1024;
  val = malloc(val_len);
  memset(val, 'O', val_len);
  // memcached allows 1mb values by default
  ck_omcache(omcache_set(oc, (cuc *) "2mbval", 6, val, val_len, 0, 0, 0, TIMEOUT), OMCACHE_TOO_LARGE_VALUE);
  val_len = 1000 * 1000;
  ck_omcache(omcache_set(oc, (cuc *) "1mbval", 6, val, val_len, 0, 0, 0, TIMEOUT), OMCACHE_OK);

  omcache_set_recv_buffer_max_size(oc, 1000);
  ck_omcache(omcache_get(oc, (cuc *) "1mbval", 6, &get_val, &get_val_len, NULL, NULL, TIMEOUT), OMCACHE_BUFFER_FULL);
  omcache_set_recv_buffer_max_size(oc, 2000000);
  ck_omcache(omcache_get(oc, (cuc *) "1mbval", 6, &get_val, &get_val_len, NULL, NULL, TIMEOUT), OMCACHE_OK);
  ck_assert_uint_eq(get_val_len, val_len);
  ck_assert_int_eq(memcmp(get_val, val, val_len), 0);
  free(val);

  omcache_free(oc);
  ot_stop_memcached(mc_port1);
  ot_stop_memcached(mc_port2);
}
END_TEST

Suite *ot_suite_commands(void)
{
  Suite *s = suite_create("Commands");
  TCase *tc_core = tcase_create("OMcache");

  tcase_add_test(tc_core, test_get);
  suite_add_tcase(s, tc_core);

  return s;
}
