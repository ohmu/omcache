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
#include "omcache_priv.h"

START_TEST(test_strerror)
{
  for (int i = OMCACHE_OK; i <= OMCACHE_SERVER_FAILURE; i ++)
    {
      const char *err = omcache_strerror(i);
      switch (i)
        {
        case OMCACHE_OK:
        case OMCACHE_NOT_FOUND:
        case OMCACHE_KEY_EXISTS:
        case OMCACHE_TOO_LARGE_VALUE:
        case OMCACHE_DELTA_BAD_VALUE:
        case OMCACHE_FAIL:
        case OMCACHE_AGAIN:
        case OMCACHE_INVALID:
        case OMCACHE_BUFFERED:
        case OMCACHE_BUFFER_FULL:
        case OMCACHE_NO_SERVERS:
        case OMCACHE_SERVER_FAILURE:
          ck_assert_int_ne(strcmp(err, "Unknown"), 0);
          break;
        default:
          ck_assert_int_eq(strcmp(err, "Unknown"), 0);
        }
    }
}
END_TEST

START_TEST(test_md5)
{
  const char text[] = "TERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION";
  const unsigned char text_md5[] = { 0xb9, 0x83, 0x21, 0xf1, 0x53, 0x89,
    0xf7, 0xd0, 0x4a, 0x1e, 0x9a, 0x8d, 0x41, 0x40, 0x3b, 0x3b };
  unsigned char buf[16];
  omc_hash_md5((unsigned char *) text, strlen(text), buf);
  ck_assert_int_eq(memcmp(text_md5, buf, 16), 0);
}
END_TEST

Suite *ot_suite_misc(void)
{
  Suite *s = suite_create("Misc");
  ot_tcase_add(s, test_strerror);
  ot_tcase_add(s, test_md5);
  return s;
}
