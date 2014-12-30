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

#include <unistd.h>
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
  // try with a too small send buffer
  omcache_set_send_buffer_max_size(oc, 5000);
  ck_omcache(omcache_set(oc, key, key_len, val, val_len, 0, 0, 0, TIMEOUT), OMCACHE_BUFFER_FULL);
  // make buffer larger, but try to send more than MC will accept
  omcache_set_send_buffer_max_size(oc, 5000000);
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

START_TEST(test_append_and_prepend)
{
  const unsigned char key[] = "test_append_and_prepend";
  size_t key_len = sizeof(key) - 1;
  const unsigned char *get_val;
  size_t val_len;
  uint64_t cas;
  omcache_t *oc = ot_init_omcache(2, LOG_INFO);

  ck_omcache(omcache_append(oc, key, key_len, (cuc *) "zxcv", 4, 0, TIMEOUT), OMCACHE_NOT_STORED);
  ck_omcache(omcache_prepend(oc, key, key_len, (cuc *) "zxcv", 4, 0, TIMEOUT), OMCACHE_NOT_STORED);

  ck_omcache_ok(omcache_set(oc, key, key_len, (cuc *) "asdf", 4, 0, 42, 0, TIMEOUT));
  ck_omcache_ok(omcache_append(oc, key, key_len, (cuc *) "!!", 2, 0, TIMEOUT));
  ck_omcache_ok(omcache_get(oc, key, key_len, &get_val, &val_len, NULL, &cas, TIMEOUT));
  ck_assert_uint_eq(val_len, 6);
  ck_assert_int_eq(memcmp(get_val, "asdf!!", 6), 0);

  ck_omcache(omcache_prepend(oc, key, key_len, (cuc *) "QWE", 3, 1, TIMEOUT), OMCACHE_KEY_EXISTS);
  ck_omcache_ok(omcache_prepend(oc, key, key_len, (cuc *) "QWE", 3, cas, TIMEOUT));
  ck_omcache_ok(omcache_get(oc, key, key_len, &get_val, &val_len, NULL, &cas, TIMEOUT));
  ck_assert_uint_eq(val_len, 9);
  ck_assert_int_eq(memcmp(get_val, "QWEasdf!!", 9), 0);

  omcache_free(oc);
}
END_TEST

START_TEST(test_touch)
{
  // touch is a new command in 1.4.8
  if (strcmp(ot_memcached_version(), "1.4.8") < 0)
    return;

  const unsigned char key[] = "test_touch";
  size_t key_len = sizeof(key) - 1;
  const unsigned char *get_val;
  size_t val_len;
  uint64_t cas;
  omcache_t *oc = ot_init_omcache(2, LOG_INFO);

  ck_omcache(omcache_touch(oc, key, key_len, 4, TIMEOUT), OMCACHE_NOT_FOUND);
  ck_omcache_ok(omcache_set(oc, key, key_len, (cuc *) "asdf", 4, 1, 0, 0, TIMEOUT));
  usleep(1100000);
  // touch should fail, the value alreayd expired
  ck_omcache(omcache_touch(oc, key, key_len, 4, TIMEOUT), OMCACHE_NOT_FOUND);
  ck_omcache_ok(omcache_set(oc, key, key_len, (cuc *) "asdf", 4, 1, 0, 0, TIMEOUT));
  ck_omcache_ok(omcache_touch(oc, key, key_len, 10, TIMEOUT));
  usleep(1500000);
  // value should've been extended by touch
  ck_omcache_ok(omcache_get(oc, key, key_len, &get_val, &val_len, NULL, &cas, TIMEOUT));
  ck_assert_uint_eq(val_len, 4);
  ck_assert_int_eq(memcmp(get_val, "asdf", 4), 0);

  omcache_free(oc);
}
END_TEST

START_TEST(test_gat)
{
  // gat is a new command in 1.4.8
  if (strcmp(ot_memcached_version(), "1.4.8") < 0)
    return;

  const unsigned char key[] = "test_gat";
  size_t key_len = sizeof(key) - 1;
  const unsigned char *get_val;
  size_t val_len;
  uint64_t cas;
  omcache_t *oc = ot_init_omcache(2, LOG_INFO);

  // set with 1 second timeout, sleep and try gat, it should fail as the value already expired
  ck_omcache_ok(omcache_set(oc, key, key_len, (cuc *) "asdf", 4, 1, 0, 0, TIMEOUT));
  usleep(1100000);
  ck_omcache(omcache_gat(oc, key, key_len, &get_val, &val_len, 4, NULL, &cas, TIMEOUT), OMCACHE_NOT_FOUND);

  // set with 1 second timeout, gat immediately, it should work as the value has not expired yet
  ck_omcache_ok(omcache_set(oc, key, key_len, (cuc *) "asdf", 4, 1, 0, 0, TIMEOUT));
  ck_omcache_ok(omcache_gat(oc, key, key_len, &get_val, &val_len, 3, NULL, &cas, TIMEOUT));
  ck_assert_uint_eq(val_len, 4);
  ck_assert_int_eq(memcmp(get_val, "asdf", 4), 0);
  // now sleep and check that the value is still there (gat should've extended its validity)
  usleep(2000000);
  ck_omcache_ok(omcache_get(oc, key, key_len, &get_val, &val_len, NULL, &cas, TIMEOUT));
  ck_assert_uint_eq(val_len, 4);
  ck_assert_int_eq(memcmp(get_val, "asdf", 4), 0);
  // NOTE: touch expiration time setting is broken in memcached <1.4.13-16-g045da59
  if (strcmp(ot_memcached_version(), "1.4.13") > 0)
    {
      // sleep some more, the value should have expired after this
      usleep(1000000);
      ck_omcache(omcache_gat(oc, key, key_len, &get_val, &val_len, 4, NULL, &cas, TIMEOUT), OMCACHE_NOT_FOUND);
    }

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

START_TEST(test_req_id_wraparound)
{
  // NOTE: omcache_t is opaque, but we need to mangle req_id for this test
  // if omcache_s layout changes this test will break in interesting ways.
  struct omcache_s_TEST
  {
    int64_t init_msec;
    uint32_t req_id;
  };
  char *keys[1000];
  size_t key_lens[1000];
  omcache_t *oc = ot_init_omcache(2, LOG_INFO);
  struct omcache_s_TEST *oc_s = (struct omcache_s_TEST *) oc;
  ck_omcache_ok(omcache_set_buffering(oc, true));
  for (int i = 0; i < 1000; i ++)
    {
      key_lens[i] = asprintf(&keys[i], "test_req_id_wraparound_%d", i);
      ck_omcache(OMCACHE_BUFFERED,
        omcache_set(oc, (cuc *) keys[i], key_lens[i], (cuc *) keys[i], key_lens[i], 0, 0, 0, 0));
    }
  ck_omcache_ok(omcache_set_buffering(oc, false));
  ck_omcache_ok(omcache_io(oc, NULL, NULL, NULL, NULL, 5000));

  // set req_id to 100 below maximum and issue a multiget for 1000 entries which should force a wraparound
  uint32_t pre_wrap_req_id = oc_s->req_id;
  oc_s->req_id = UINT32_MAX - 100;

  omcache_value_t values[1000];
  size_t value_count = 1000, values_found = 0;
  omcache_req_t reqs[1000];
  size_t req_count = 1000;
  ck_omcache_ok_or_again(omcache_get_multi(oc, (cuc **) keys, key_lens, 1000, reqs, &req_count, values, &value_count, 5000));
  values_found = value_count;
  while (req_count > 0)
    {
      value_count = 1000;
      ck_omcache_ok_or_again(omcache_io(oc, reqs, &req_count, values, &value_count, 5000));
      values_found += value_count;
    }
  ck_assert_int_eq(values_found, 1000);
  ck_assert_uint_le(oc_s->req_id, pre_wrap_req_id);
  for (int i = 0; i < 1000; i ++)
    free(keys[i]);

  omcache_free(oc);
}
END_TEST

START_TEST(test_buffering)
{
  char *keys[1000];
  size_t key_lens[1000];
  omcache_t *oc = ot_init_omcache(3, LOG_INFO);
  ck_omcache_ok(omcache_set_buffering(oc, true));
  for (int i = 0; i < 1000; i += 2)
    {
      key_lens[i] = asprintf(&keys[i], "test_buffering_%d", i);
      ck_omcache(OMCACHE_BUFFERED,
        omcache_set(oc, (cuc *) keys[i], key_lens[i], (cuc *) keys[i], key_lens[i], 0, 0, 0, 0));
    }
  ck_omcache_ok(omcache_reset_buffers(oc));
  for (int i = 1; i < 1000; i += 2)
    {
      key_lens[i] = asprintf(&keys[i], "test_buffering_%d", i);
      ck_omcache(OMCACHE_BUFFERED,
        omcache_set(oc, (cuc *) keys[i], key_lens[i], (cuc *) keys[i], key_lens[i], 0, 0, 0, 0));
    }
  ck_omcache_ok(omcache_set_buffering(oc, false));
  ck_omcache_ok(omcache_io(oc, NULL, NULL, NULL, NULL, 5000));

  // no even keys should be set
  omcache_value_t values[1000];
  size_t value_count = 1000, values_found = 0;
  omcache_req_t reqs[1000];
  size_t req_count = 1000;
  ck_omcache_ok_or_again(omcache_get_multi(oc, (cuc **) keys, key_lens, 1000, reqs, &req_count, values, &value_count, 5000));
  values_found = value_count;
  while (req_count > 0)
    {
      value_count = 1000;
      ck_omcache_ok_or_again(omcache_io(oc, reqs, &req_count, values, &value_count, 5000));
      values_found += value_count;
      // check that the last character of key is odd (ord("0") % 2 == 0)
      for (size_t i = 0; i < value_count; i ++)
        ck_assert_int_eq(1, values[i].key[values[i].key_len - 1] % 2);
    }
  ck_assert_int_eq(values_found, 500);

  // test trying to fetch non-matching request range
  // omcache_get_multi fails with a too low value count
  req_count = 999;
  ck_omcache(OMCACHE_INVALID,
    omcache_io(oc, reqs, &req_count, NULL, NULL, 5000));
  // but omcache_stat doesn't really know how many values we will receive,
  // so it'll accept the request but silently drop some messages
  value_count = 1;
  ck_omcache_ok(omcache_stat(oc, "", values, &value_count, 0, 5000));
  for (int i = 0; i < 1000; i ++)
    free(keys[i]);

  omcache_free(oc);
}
END_TEST

static void test_response_callback_cb(omcache_t *mc __attribute__((unused)),
                                      omcache_value_t *result, void *context)
{
  size_t *values_found_p = (size_t *) context;
  // NOTE: don't compare keys, memcached's GATKQ handling doesn't return
  // keys currently https://github.com/memcached/memcached/pull/85
  if (result->status == OMCACHE_OK &&
      result->data_len >= sizeof("test_response_callback_") &&
      memcmp(result->data, "test_response_callback_", sizeof("test_response_callback_") - 1) == 0)
    {
      *values_found_p = (*values_found_p) + 1;
    }
}

START_TEST(test_response_callback)
{
  char *keys[64];
  size_t key_lens[64];
  size_t values_found = 0;
  omcache_t *oc = ot_init_omcache(1, LOG_INFO);
  ck_omcache_ok(omcache_set_response_callback(oc, test_response_callback_cb, &values_found));
  for (int i = 0; i < 64; i ++)
    {
      key_lens[i] = asprintf(&keys[i], "test_response_callback_%d", i);
      // only set half of the keys
      if (i % 2)
        continue;
      ck_omcache(OMCACHE_BUFFERED,
        omcache_set(oc, (cuc *) keys[i], key_lens[i], (cuc *) keys[i], key_lens[i], 0, 0, 0, 0));
    }
  ck_omcache_ok(omcache_set_buffering(oc, false));
  ck_omcache_ok(omcache_io(oc, NULL, NULL, NULL, NULL, 5000));

  // no even keys should be set
  omcache_req_t reqs[64];
  size_t req_count = 64;
  ck_omcache_ok_or_again(omcache_get_multi(oc, (cuc **) keys, key_lens, 64, reqs, &req_count, NULL, NULL, 5000));
  while (req_count > 0)
    ck_omcache_ok_or_again(omcache_io(oc, reqs, &req_count, NULL, NULL, 5000));
  ck_assert_int_eq(values_found, 32);

  // now do the same thing with GAT (get-and-touch)
  req_count = 64;
  values_found = 0;
  time_t expirations[64];
  for (int i = 0; i < 64; i ++)
    expirations[i] = 10 + i;
  ck_omcache_ok_or_again(omcache_gat_multi(oc, (cuc **) keys, key_lens, expirations, 64, reqs, &req_count, NULL, NULL, 5000));
  while (req_count > 0)
    ck_omcache_ok_or_again(omcache_io(oc, reqs, &req_count, NULL, NULL, 5000));
  ck_assert_int_eq(values_found, 32);

  for (int i = 0; i < 64; i ++)
    free(keys[i]);
  omcache_free(oc);
}
END_TEST

Suite *ot_suite_commands(void)
{
  Suite *s = suite_create("Commands");

  ot_tcase_add(s, test_noop);
  ot_tcase_add(s, test_stats);
  ot_tcase_add(s, test_flush_all);
  ot_tcase_add(s, test_set_get_delete);
  ot_tcase_add(s, test_cas_and_flags);
  ot_tcase_add(s, test_add_and_replace);
  ot_tcase_add(s, test_append_and_prepend);
  tcase_set_timeout(ot_tcase_add(s, test_touch), 10);
  tcase_set_timeout(ot_tcase_add(s, test_gat), 10);
  ot_tcase_add(s, test_increment_and_decrement);
  ot_tcase_add(s, test_req_id_wraparound);
  ot_tcase_add(s, test_buffering);
  ot_tcase_add(s, test_response_callback);

  return s;
}
