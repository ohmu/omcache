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
  omcache_t *oc = ot_init_omcache(0, LOG_INFO);
  ck_assert_int_eq(omcache_server_index_for_key(oc, (cuc *) "foo", 3), 0);
  ck_assert_ptr_eq(omcache_server_info(oc, 0), NULL);
  // NOTE: omcache sorts server list internally, host and portnames are not
  // checked when server list is created, they're resolved when we actually
  // try to connect so invalid entries can be pushed to the list
  ck_omcache_ok(omcache_set_servers(oc, "8.8.8.8:22,,   127.0.0.1:11300  , 10.0.0.0, 10.10.10.10:11111"));
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
  ck_omcache_ok(omcache_set_servers(oc, ""));
  omcache_free(oc);
}
END_TEST

START_TEST(test_no_servers)
{
  omcache_t *oc = ot_init_omcache(0, LOG_INFO);
  ck_omcache(omcache_noop(oc, 0, 0), OMCACHE_NO_SERVERS);
  ck_omcache_ok(omcache_io(oc, NULL, NULL, NULL, NULL, 0));
  omcache_free(oc);
}
END_TEST

START_TEST(test_invalid_servers)
{
  omcache_t *oc = ot_init_omcache(0, LOG_INFO);
  ck_omcache_ok(omcache_set_servers(oc, "127.0.0.1:1, 127.0.0.1:22, 127.0.0.foobar:asdf,,,"));

  // since omcache starts with good faith in servers it's given it won't
  // actually notice that the first two servers are dead or not talking
  // memcached protocol before trying to communicate with them and then
  // dropping the connections.  the third one will fail immediately as it's
  // address can't be resolved.
  ck_omcache(omcache_noop(oc, 0, 2000), OMCACHE_NO_SERVERS);
  ck_omcache(omcache_noop(oc, 1, 2000), OMCACHE_NO_SERVERS);
  ck_omcache(omcache_noop(oc, 1, 2000), OMCACHE_NO_SERVERS);
  ck_omcache(omcache_get(oc, (cuc *) "foo", 3, NULL, NULL, NULL, NULL, 2000), OMCACHE_NO_SERVERS);

  omcache_free(oc);
}
END_TEST

START_TEST(test_multiple_times_same_server)
{
  omcache_t *oc = ot_init_omcache(1, LOG_INFO);
  char sbuf[sizeof("127.0.0.1:11211,") * 20 + 1], *p = sbuf;

  ck_omcache(omcache_get(oc, (cuc *) "foo", 3, NULL, NULL, NULL, NULL, 2000), OMCACHE_NOT_FOUND);
  for (int i = 0; i < 20; i++)
    p += sprintf(p, "%s127.0.0.1:%d", i == 0 ? "" : ",", ot_get_memcached(0));
  ck_omcache_ok(omcache_set_servers(oc, sbuf));
  ck_omcache(omcache_get(oc, (cuc *) "foo", 3, NULL, NULL, NULL, NULL, 2000), OMCACHE_NOT_FOUND);
  for (int i = 0; i < 20; i++)
    ck_omcache_ok(omcache_noop(oc, i, 1000));

  omcache_free(oc);
}
END_TEST

START_TEST(test_ipv6)
{
  // NOTE: memcached doesn't support specifying a literal IPv6 address on
  // the commandline, so we try to use 'localhost6' if we can find it in
  // /etc/hosts
  char linebuf[300];
  bool have_localhost6 = false;
  FILE *fp = fopen("/etc/hosts", "r");
  if (fp == NULL)
    return;
  while (!have_localhost6)
    {
      if (fgets(linebuf, sizeof(linebuf), fp) == NULL)
        break;
      if (strstr(linebuf, "localhost6") != NULL)
        have_localhost6 = true;
    }
  fclose(fp);
  if (!have_localhost6)
    return;
  omcache_t *oc = ot_init_omcache(0, LOG_DEBUG);
  int mc_port = ot_start_memcached("localhost6", NULL);
  sprintf(linebuf, "[::1]:%d", mc_port);
  ck_omcache_ok(omcache_set_servers(oc, linebuf));
  ck_omcache_ok(omcache_noop(oc, 0, 1000));
  omcache_free(oc);
}
END_TEST

Suite *ot_suite_servers(void)
{
  Suite *s = suite_create("Servers");

  ot_tcase_add(s, test_server_list);
  ot_tcase_add(s, test_no_servers);
  ot_tcase_add(s, test_invalid_servers);
  ot_tcase_add(s, test_multiple_times_same_server);
  ot_tcase_add(s, test_ipv6);

  return s;
}
