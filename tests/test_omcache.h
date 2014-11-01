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

#ifndef _TEST_OMCACHE_H
#define _TEST_OMCACHE_H 1

#include <check.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syslog.h>
#include "omcache.h"

/* Check 0.9.8 which is available on Debian 6 and 7 doesn't support any
 * unsigned operations or greater / lesser comparisons, but as the macros
 * don't consider data types at all we can just use the int comparison for
 * everything there.
 * Also, older Check versions evaluate macro arguments multiple times.
 */
#if CHECK_MAJOR_VERSION * 10000 + CHECK_MINOR_VERSION * 100 + CHECK_MICRO_VERSION < 910
#define ck_assert_int_ge(X, Y) _ck_assert_int(X, >=, Y)
#define ck_assert_int_le(X, Y) _ck_assert_int(X, <=, Y)
#define ck_assert_uint_eq(X, Y) _ck_assert_int(X, ==, Y)
#define ck_assert_uint_ne(X, Y) _ck_assert_int(X, !=, Y)
#define ck_assert_uint_ge(X, Y) _ck_assert_int(X, >=, Y)
#define ck_assert_uint_le(X, Y) _ck_assert_int(X, <=, Y)
#define ck_assert_ptr_eq(X, Y) _ck_assert_int(X, ==, Y)
#define ck_assert_ptr_ne(X, Y) _ck_assert_int(X, !=, Y)
#define ck_omcache(c,e) ({ int omc_ret = (c); ck_assert_int_eq(omc_ret, (e)); })
#else
#define ck_omcache(c,e) ck_assert_int_eq((c), (e))
#endif

#define ck_omcache_ok(c) ck_omcache((c), OMCACHE_OK)

typedef const unsigned char cuc;

omcache_t *ot_init_omcache(int server_count, int log_level);
int ot_get_memcached(size_t server_index);
int ot_start_memcached(const char *addr);
int ot_stop_memcached(int port);

Suite *ot_suite_commands(void);
Suite *ot_suite_servers(void);

typedef Suite *(suite_init_cb)(void);
static suite_init_cb *const suites[] = {
  ot_suite_commands, ot_suite_servers,
  };

#endif // !_TEST_OMCACHE_H
