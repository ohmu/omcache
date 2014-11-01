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

START_TEST(test_server_list)
{
  omcache_t *oc = ot_init_omcache(0, LOG_DEBUG);
  ck_assert_int_eq(omcache_server_index_for_key(oc, (cuc *) "foo", 3), 0);
  ck_assert_ptr_eq(omcache_server_info(oc, 0), NULL);
  // NOTE: omcache sorts server list internally, host and portnames are not
  // checked when server list is created, they're resolved when we actually
  // try to connect so invalid entries can be pushed to the list
  ck_omcache_ok(omcache_set_servers(oc, "127.0.0.1:11300, 10.0.0.0, 10.10.10.10:11111, 192.168.255.255:99999"));
  for (int i = 0; i < 4; i ++)
    {
      omcache_server_info_t *sinfo = omcache_server_info(oc, i);
      ck_assert_ptr_ne(sinfo, NULL);
      ck_omcache(sinfo->omcache_version, OMCACHE_VERSION);
      ck_assert_int_eq(sinfo->server_index, i);
      switch (i)
        {
        case 0:
          ck_assert_int_eq(sinfo->port, 11211);
          ck_assert_str_eq(sinfo->hostname, "10.0.0.0");
          break;
        case 1:
          ck_assert_int_eq(sinfo->port, 11111);
          ck_assert_str_eq(sinfo->hostname, "10.10.10.10");
          break;
        case 2:
          ck_assert_int_eq(sinfo->port, 11300);
          ck_assert_str_eq(sinfo->hostname, "127.0.0.1");
          break;
        case 3:
          ck_assert_int_eq(sinfo->port, 99999);
          ck_assert_str_eq(sinfo->hostname, "192.168.255.255");
          break;
        default:
          ck_assert(false);
        }
      ck_omcache_ok(omcache_server_info_free(oc, sinfo));
    }
  // check that we have a mostly even distribution
  int hits[4] = {0};
  for (int i = 0;  i < 1000; i ++)
    {
      int si = omcache_server_index_for_key(oc, (cuc *) &i, sizeof(i));
      ck_assert_int_ge(si, 0);
      ck_assert_int_le(si, 3);
      hits[si] ++;
    }
  for (int i = 0; i < 4; i++)
    {
      ck_assert_int_ge(hits[i], 200);
      ck_assert_int_le(hits[i], 300);
    }
  omcache_free(oc);
}
END_TEST

START_TEST(test_no_servers)
{
  omcache_t *oc = ot_init_omcache(0, LOG_DEBUG);
  ck_omcache(omcache_noop(oc, 0, 0), OMCACHE_NO_SERVERS);
  ck_omcache_ok(omcache_io(oc, NULL, NULL, NULL, NULL, 0));
  omcache_free(oc);
}
END_TEST

Suite *ot_suite_servers(void)
{
  Suite *s = suite_create("Servers");
  TCase *tc_core = tcase_create("OMcache");

  tcase_add_test(tc_core, test_server_list);
  tcase_add_test(tc_core, test_no_servers);
  suite_add_tcase(s, tc_core);

  return s;
}
