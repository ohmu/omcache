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

  // we'll get OMCACHE_NOT_FOUND after this server times out in 1 second
  int64_t begin = ot_msec();
  ck_omcache(omcache_get(oc, (cuc *) strbuf, key_len, &val, &val_len, NULL, NULL, -1), OMCACHE_NOT_FOUND);
  ck_assert_int_le(ot_msec() - begin, 1500);
  begin = ot_msec();
  ck_omcache(omcache_get(oc, (cuc *) strbuf, key_len, &val, &val_len, NULL, NULL, -1), OMCACHE_NOT_FOUND);
  ck_assert_int_le(ot_msec() - begin, 1500);
  // now the server should be disabled and fail faster
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
  ck_omcache_ok(omcache_get(oc, (cuc *) strbuf, key_len, &val, &val_len, NULL, NULL, -1));
  ck_assert_int_eq(susp_server_index, omcache_server_index_for_key(oc, (cuc *) strbuf, key_len));

  omcache_free(oc);
}
END_TEST

Suite *ot_suite_failures(void)
{
  Suite *s = suite_create("Failures");
  TCase *tc_core = tcase_create("OMcache");

  tcase_add_test(tc_core, test_suspended_memcache);
  tcase_set_timeout(tc_core, 60);
  suite_add_tcase(s, tc_core);

  return s;
}
