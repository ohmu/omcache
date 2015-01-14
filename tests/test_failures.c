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
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>

#define TIMEOUT 2000

START_TEST(test_suspended_memcache)
{
  char strbuf[2048];
  const unsigned char *val;
  size_t val_len, key_len;
  omcache_t *oc = ot_init_omcache(0, LOG_INFO);

  int susp_server_index = -1;
  int mc_port0, mc_port1, mc_port2;
  pid_t mc_pid0, mc_pid1, mc_pid2;

  mc_port0 = ot_start_memcached(NULL, &mc_pid0);
  mc_port1 = ot_start_memcached(NULL, &mc_pid1);
  mc_port2 = ot_start_memcached(NULL, &mc_pid2);

  sprintf(strbuf, "127.0.0.1:%d,127.0.0.1:%d,127.0.0.1:%d", mc_port0, mc_port1, mc_port2);
  ck_omcache_ok(omcache_set_servers(oc, strbuf));
  ck_omcache_ok(omcache_set_dead_timeout(oc, 1000));
  ck_omcache_ok(omcache_set_connect_timeout(oc, 3000));
  ck_omcache_ok(omcache_set_reconnect_timeout(oc, 4000));
  ck_omcache_ok(omcache_set_buffering(oc, true));
  for (int i = 0; i < 1000; i ++)
    {
      key_len = snprintf(strbuf, sizeof(strbuf), "test_suspended_memcache_%d", i);
      ck_omcache(OMCACHE_BUFFERED,
        omcache_set(oc, (cuc *) strbuf, key_len, (cuc *) strbuf, key_len, 0, 0, 0, 0));
    }
  ck_omcache_ok(omcache_set_buffering(oc, false));
  ck_omcache_ok(omcache_io(oc, NULL, NULL, NULL, NULL, 5000));

  // suspend one memcached and find out its server index
  kill(mc_pid2, SIGSTOP);
  usleep(100000);  // allow 0.1 for SIGSTOP to be delivered
  for (int i = 0; i < 3; i ++)
    {
      omcache_server_info_t *sinfo = omcache_server_info(oc, i);
      if (sinfo->port == mc_port2)
        susp_server_index = i;
      ck_omcache_ok(omcache_server_info_free(oc, sinfo));
    }
  ck_assert_int_ge(susp_server_index, 0);

  // get a key that belongs to the suspended server
  for (int i = 0; i < 1000; i ++)
    {
      key_len = snprintf(strbuf, sizeof(strbuf), "test_suspended_memcache_%d", i);
      if (omcache_server_index_for_key(oc, (cuc *) strbuf, key_len) == susp_server_index)
        break;
      *strbuf = 0;
    }
  ck_assert_int_ne(*strbuf, 0);

  // we'll get OMCACHE_SERVER_FAILURE after this server times out in 1 second
  int64_t begin = ot_msec();
  ck_omcache(omcache_get(oc, (cuc *) strbuf, key_len, &val, &val_len, NULL, NULL, -1), OMCACHE_SERVER_FAILURE);
  ck_assert_int_le(ot_msec() - begin, 1500);
  begin = ot_msec();
  ck_omcache(omcache_get(oc, (cuc *) strbuf, key_len, &val, &val_len, NULL, NULL, -1), OMCACHE_SERVER_FAILURE);
  ck_assert_int_le(ot_msec() - begin, 1500);
  // now the server should be disabled and fail faster with NOT_FOUND as we're not accessing the failed server anymore
  begin = ot_msec();
  ck_omcache(omcache_get(oc, (cuc *) strbuf, key_len, &val, &val_len, NULL, NULL, -1), OMCACHE_NOT_FOUND);
  ck_assert_int_le(ot_msec() - begin, 500);
  ck_assert_int_ne(susp_server_index, omcache_server_index_for_key(oc, (cuc *) strbuf, key_len));

  // wait for the server to come back online after reconnect timeout
  kill(mc_pid2, SIGCONT);
  sleep(5);
  // the first lookup should fail, but it should cause reconnection phase to start
  begin = ot_msec();
  ck_omcache(omcache_get(oc, (cuc *) strbuf, key_len, &val, &val_len, NULL, NULL, -1), OMCACHE_NOT_FOUND);
  ck_assert_int_le(ot_msec() - begin, 500);
  // try to read the value a few times, this can fail a few more times
  // before the connection has been recreated and confirmed
  int ret = -1;
  for (int i = 0; (i < 10) && (ret != OMCACHE_OK); i ++)
    {
      if (i != 0 && ret != OMCACHE_OK)
        usleep(100000);
      ret = omcache_get(oc, (cuc *) strbuf, key_len, &val, &val_len, NULL, NULL, -1);
    }
  ck_assert_int_eq(ret, OMCACHE_OK);
  ck_assert_int_eq(susp_server_index, omcache_server_index_for_key(oc, (cuc *) strbuf, key_len));

  omcache_free(oc);
}
END_TEST

START_TEST(test_all_backends_fail)
{
  size_t item_count = 10;
  const unsigned char keydata[] =
    "342f48a2c3a152a0fe39df4f2bca34d3c6c56e57797f0da682a6154ef7b674e3"
    "9c131c0c70442f94b865a5e0e030b48f4f51969fb80d5251fd67023c9982d3ab"
    "1ffd27717200ccb3c92882b10a04129422d5b71ddfaf24daf9fb5ee9cdfa2ef0";
  size_t val_len;
  const unsigned char *val;
  pid_t mc_pid0, mc_pid1;
  int mc_port0 = ot_start_memcached(NULL, &mc_pid0);
  int mc_port1 = ot_start_memcached(NULL, &mc_pid1);
  char strbuf[100];
  sprintf(strbuf, "127.0.0.1:%d,127.0.0.1:%d", mc_port0, mc_port1);

  omcache_t *oc = ot_init_omcache(0, LOG_INFO);
  ck_omcache_ok(omcache_set_servers(oc, strbuf));
  ck_omcache_ok(omcache_set_dead_timeout(oc, 1000));
  ck_omcache_ok(omcache_set_connect_timeout(oc, 2000));
  ck_omcache_ok(omcache_set_reconnect_timeout(oc, 3000));

  // send noops to both servers and write a bunch of values to them to make
  // sure we're connected to both servers and ketama picks both servers
  ck_omcache_ok(omcache_noop(oc, 0, 1000));
  ck_omcache_ok(omcache_noop(oc, 1, 1000));

  for (size_t i = 0; i < item_count; i ++)
    ck_omcache(omcache_set(oc, keydata + i, 100, keydata + i, 100, 0, 0, 0, 0), OMCACHE_BUFFERED);
  ck_omcache_ok(omcache_io(oc, NULL, NULL, NULL, NULL, 5000));
  for (size_t i = 0; i < item_count; i ++)
    {
      ck_omcache_ok(omcache_get(oc, keydata + i, 100, &val, &val_len, NULL, NULL, 3000));
      ck_assert_uint_eq(val_len, 100);
      ck_assert_int_eq(memcmp(val, keydata + i, 100), 0);
    }

  // suspend memcaches
  kill(mc_pid0, SIGSTOP);
  kill(mc_pid1, SIGSTOP);
  usleep(100000);  // allow 0.1 for SIGSTOPs to be delivered

  // now try to read the values
  for (size_t i = 0; i < item_count; i ++)
    {
      int ret = omcache_get(oc, keydata + i, 100, &val, &val_len, NULL, NULL, 3000);
      ck_assert_int_ne(ret, OMCACHE_OK);
    }

  // sleep over timeouts again and try again
  sleep(3);

  // resume one memcache
  kill(mc_pid0, SIGCONT);

  // now try to read the values again, some of these will fail because we're
  // not yet fully connected to mc_pid0 and mc_pid1 is still down
  size_t found = 0;
  for (size_t i = 0; i < item_count; i ++)
    {
      int ret = omcache_get(oc, keydata + i, 100, &val, &val_len, NULL, NULL, 3000);
      if (ret == OMCACHE_OK)
        {
          ck_assert_uint_eq(val_len, 100);
          ck_assert_int_eq(memcmp(val, keydata + i, 100), 0);
          found ++;
        }
      else
        {
          usleep(1000);
        }
    }
  ck_assert_uint_ge(found, 1);  // we should've found something

  // resume the other memcache
  kill(mc_pid1, SIGCONT);

  // sleep over timeouts again
  sleep(3);

  // try to read the values yet again, some of these will fail because we're
  // not yet fully connected after resuming mc_pid1
  found = 0;
  for (size_t i = 0; i < item_count; i ++)
    {
      int ret = omcache_get(oc, keydata + i, 100, &val, &val_len, NULL, NULL, 3000);
      if (ret == OMCACHE_OK)
        {
          ck_assert_uint_eq(val_len, 100);
          ck_assert_int_eq(memcmp(val, keydata + i, 100), 0);
          found ++;
        }
      else
        {
          usleep(1000);
        }
    }
  ck_assert_uint_ge(found, item_count / 2 + 1);  // we should've found more than half

  // sleep over timeouts again
  sleep(3);

  // try to read the values a final time, now we should have everything
  for (size_t i = 0; i < item_count; i ++)
    {
      ck_omcache_ok(omcache_get(oc, keydata + i, 100, &val, &val_len, NULL, NULL, 3000));
      ck_assert_uint_eq(val_len, 100);
      ck_assert_int_eq(memcmp(val, keydata + i, 100), 0);
    }

  omcache_free(oc);
}
END_TEST

Suite *ot_suite_failures(void)
{
  Suite *s = suite_create("Failures");
  ot_tcase_add_timeout(s, test_suspended_memcache, 60);
  ot_tcase_add_timeout(s, test_all_backends_fail, 60);

  return s;
}
