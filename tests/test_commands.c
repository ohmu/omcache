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

START_TEST(test_noop)
{
  omcache_t *oc = ot_init_omcache(2, LOG_INFO);

  ck_omcache_ok(omcache_noop(oc, 0, TIMEOUT));
  ck_omcache_ok(omcache_noop(oc, 1, TIMEOUT));
  ck_omcache(omcache_noop(oc, 2, TIMEOUT), OMCACHE_NO_SERVERS);

  omcache_free(oc);
}
END_TEST

START_TEST(test_stats)
{
  omcache_t *oc = ot_init_omcache(3, LOG_INFO);
  omcache_value_t vals[100];

  for (int i = 0; i < 3; i ++)
    {
      size_t val_count = sizeof(vals) / sizeof(vals[0]);
      ck_omcache_ok(omcache_stat(oc, NULL, vals, &val_count, i, TIMEOUT));
      ck_assert_uint_ge(val_count, 10);
    }

  omcache_free(oc);
}
END_TEST

START_TEST(test_flush_all)
{
  omcache_t *oc = ot_init_omcache(2, LOG_INFO);
  const unsigned char key[] = "test_flush_all";
  size_t key_len = sizeof(key) - 1;
  const unsigned char *get_val;
  size_t get_val_len;

  ck_omcache_ok(omcache_flush_all(oc, 0, 0, TIMEOUT));
  ck_omcache_ok(omcache_flush_all(oc, 0, 1, TIMEOUT));

  ck_omcache_ok(omcache_set(oc, key, key_len, (cuc *) "bar", 3, 0, 42, 0, TIMEOUT));
  ck_omcache_ok(omcache_get(oc, key, key_len, &get_val, &get_val_len, NULL, NULL, TIMEOUT));
  ck_assert_uint_eq(get_val_len, 3);

  ck_omcache_ok(omcache_flush_all(oc, 0, 0, TIMEOUT));
  ck_omcache_ok(omcache_flush_all(oc, 0, 1, TIMEOUT));

  ck_omcache(omcache_get(oc, key, key_len, &get_val, &get_val_len, NULL, NULL, TIMEOUT), OMCACHE_NOT_FOUND);

  omcache_free(oc);
}
END_TEST

START_TEST(test_set_get_delete)
{
  unsigned char *val;
  const unsigned char key[] = "test_set_get_delete";
  size_t key_len = sizeof(key) - 1;
  const unsigned char *get_val;
  size_t val_len, get_val_len;
  uint32_t flags;
  uint64_t cas;
  omcache_t *oc = ot_init_omcache(2, LOG_INFO);

  ck_omcache(omcache_get(oc, key, key_len, NULL, NULL, NULL, NULL, TIMEOUT), OMCACHE_NOT_FOUND);
  ck_omcache_ok(omcache_set(oc, key, key_len, (cuc *) "bar", 3, 0, 42, 0, TIMEOUT));

  ck_omcache_ok(omcache_get(oc, key, key_len, &get_val, &val_len, &flags, &cas, TIMEOUT));
  ck_assert_uint_eq(val_len, 3);
  ck_assert_int_eq(memcmp(get_val, "bar", 3), 0);
  ck_assert_uint_eq(flags, 42);
  ck_assert(cas != 0);

  ck_omcache_ok(omcache_delete(oc, key, key_len, TIMEOUT));
  ck_omcache(omcache_delete(oc, key, key_len, TIMEOUT), OMCACHE_NOT_FOUND);
  ck_omcache(omcache_get(oc, key, key_len, NULL, NULL, NULL, NULL, TIMEOUT), OMCACHE_NOT_FOUND);

  // memcached allows 1mb values by default
  val_len = 2048 * 1024;
  val = malloc(val_len);
  memset(val, 'O', val_len);
  // memcached allows 1mb values by default
  ck_omcache(omcache_set(oc, key, key_len, val, val_len, 0, 0, 0, TIMEOUT), OMCACHE_TOO_LARGE_VALUE);
  val_len = 1000 * 1000;
  ck_omcache_ok(omcache_set(oc, key, key_len, val, val_len, 0, 0, 0, TIMEOUT));

  omcache_set_recv_buffer_max_size(oc, 1000);
  ck_omcache(omcache_get(oc, key, key_len, &get_val, &get_val_len, NULL, NULL, TIMEOUT), OMCACHE_BUFFER_FULL);
  omcache_set_recv_buffer_max_size(oc, 2000000);
  ck_omcache_ok(omcache_get(oc, key, key_len, &get_val, &get_val_len, NULL, NULL, TIMEOUT));
  ck_assert_uint_eq(get_val_len, val_len);
  ck_assert_int_eq(memcmp(get_val, val, val_len), 0);
  free(val);

  omcache_free(oc);
}
END_TEST

START_TEST(test_cas_and_flags)
{
  const unsigned char key[] = "test_cas_and_flags";
  size_t key_len = sizeof(key) - 1;
  const unsigned char *get_val;
  size_t val_len;
  uint32_t flags;
  uint64_t cas, cas2;
  omcache_t *oc = ot_init_omcache(2, LOG_INFO);

  ck_omcache_ok(omcache_set(oc, key, key_len, (cuc *) "bar", 3, 0, 42, 0, TIMEOUT));
  ck_omcache_ok(omcache_get(oc, key, key_len, &get_val, &val_len, &flags, &cas, TIMEOUT));
  ck_assert_uint_eq(val_len, 3);
  ck_assert_int_eq(memcmp(get_val, "bar", 3), 0);
  ck_assert_uint_eq(flags, 42);
  ck_assert(cas != 0);

  ck_omcache(omcache_set(oc, key, key_len, (cuc *) "baz", 3, 0, 42, 0xdeadbeef, TIMEOUT), OMCACHE_KEY_EXISTS);
  ck_omcache(omcache_set(oc, key, key_len, (cuc *) "baz", 3, 0, 42, cas, TIMEOUT), OMCACHE_OK);
  ck_omcache_ok(omcache_get(oc, key, key_len, &get_val, &val_len, &flags, &cas2, TIMEOUT));
  ck_assert_uint_eq(val_len, 3);
  ck_assert_uint_eq(flags, 42);
  ck_assert_uint_ne(cas, cas2);

  omcache_free(oc);
}
END_TEST

START_TEST(test_add_and_replace)
{
  const unsigned char key[] = "test_add_and_replace";
  size_t key_len = sizeof(key) - 1;
  const unsigned char *get_val;
  size_t val_len;
  uint32_t flags;
  uint64_t cas;
  omcache_t *oc = ot_init_omcache(2, LOG_INFO);

  ck_omcache(omcache_replace(oc, key, key_len, (cuc *) "zxcv", 4, 0, 99, TIMEOUT), OMCACHE_NOT_FOUND);
  ck_omcache_ok(omcache_set(oc, key, key_len, (cuc *) "asdf", 4, 0, 42, 0, TIMEOUT));
  ck_omcache_ok(omcache_replace(oc, key, key_len, (cuc *) "bar", 3, 0, 99, TIMEOUT));
  ck_omcache(omcache_add(oc, key, key_len, (cuc *) "zxcv", 4, 0, 99, TIMEOUT), OMCACHE_KEY_EXISTS);
  ck_omcache_ok(omcache_get(oc, key, key_len, &get_val, &val_len, &flags, &cas, TIMEOUT));
  ck_assert_uint_eq(val_len, 3);
  ck_assert_int_eq(memcmp(get_val, "bar", 3), 0);
  ck_assert_uint_eq(flags, 99);
  ck_assert(cas != 0);

  omcache_free(oc);
}
END_TEST

START_TEST(test_increment_and_decrement)
{
  const unsigned char key[] = "test_increment_and_decrement";
  size_t key_len = sizeof(key) - 1;
  const unsigned char *get_val;
  size_t val_len;
  uint64_t val_uint = 0;
  omcache_t *oc = ot_init_omcache(2, LOG_INFO);

  ck_omcache_ok(omcache_set(oc, key, key_len, (cuc *) "asdf", 4, 0, 42, 0, TIMEOUT));
  ck_omcache(omcache_increment(oc, key, key_len, 12, 0, 0, &val_uint, TIMEOUT), OMCACHE_DELTA_BAD_VALUE);
  ck_assert_uint_eq(val_uint, 0);

  ck_omcache_ok(omcache_delete(oc, key, key_len, TIMEOUT));
  ck_omcache_ok(omcache_increment(oc, key, key_len, 12, 3, 0, &val_uint, TIMEOUT));
  ck_assert_uint_eq(val_uint, 3);
  ck_omcache_ok(omcache_increment(oc, key, key_len, 12, 0, 0, &val_uint, TIMEOUT));
  ck_assert_uint_eq(val_uint, 15);
  ck_omcache_ok(omcache_get(oc, key, key_len, &get_val, &val_len, NULL, NULL, TIMEOUT));
  ck_assert_uint_eq(val_len, 2);
  ck_assert_int_eq(memcmp(get_val, "15", 2), 0);
  ck_omcache_ok(omcache_decrement(oc, key, key_len, 1000, 3, 0, &val_uint, TIMEOUT));
  ck_assert_uint_eq(val_uint, 0);
  ck_omcache(omcache_increment(oc, key + 1, key_len - 1, 1000, 1000, OMCACHE_DELTA_NO_ADD, &val_uint, TIMEOUT),
    OMCACHE_NOT_FOUND);
  ck_omcache(omcache_decrement(oc, key + 1, key_len - 1, 1000, 1000, OMCACHE_DELTA_NO_ADD, &val_uint, TIMEOUT),
    OMCACHE_NOT_FOUND);
  ck_assert_uint_eq(val_uint, 0);
  ck_omcache_ok(omcache_decrement(oc, key + 1, key_len - 1, 1000, 999, 0, &val_uint, TIMEOUT));
  ck_assert_uint_eq(val_uint, 999);
  ck_omcache_ok(omcache_decrement(oc, key + 1, key_len - 1, 10, 999, 0, &val_uint, TIMEOUT));
  ck_assert_uint_eq(val_uint, 989);
  ck_omcache_ok(omcache_increment(oc, key + 1, key_len - 1, 20, 999, 0, &val_uint, TIMEOUT));
  ck_assert_uint_eq(val_uint, 1009);

  omcache_free(oc);
}
END_TEST

Suite *ot_suite_commands(void)
{
  Suite *s = suite_create("Commands");
  TCase *tc_core = tcase_create("OMcache");

  tcase_add_test(tc_core, test_noop);
  tcase_add_test(tc_core, test_stats);
  tcase_add_test(tc_core, test_flush_all);
  tcase_add_test(tc_core, test_set_get_delete);
  tcase_add_test(tc_core, test_cas_and_flags);
  tcase_add_test(tc_core, test_add_and_replace);
  tcase_add_test(tc_core, test_increment_and_decrement);
  suite_add_tcase(s, tc_core);

  return s;
}
