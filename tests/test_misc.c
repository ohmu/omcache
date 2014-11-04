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

Suite *ot_suite_misc(void)
{
  Suite *s = suite_create("Misc");
  ot_tcase_add(s, test_strerror);
  return s;
}
