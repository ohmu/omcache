/*
 * OMcache: portability macros and functions
 *
 * Copyright (c) 2014, Oskari Saarenmaa <os@ohmu.fi>
 * All rights reserved.
 *
 * This file is under the Apache License, Version 2.0.
 * See the file `LICENSE` for details.
 *
 */

#ifndef _OMCACHE_COMPAT_H
#define _OMCACHE_COMPAT_H 1

#include <unistd.h>

#define max(a,b) ({__typeof__(a) a_ = (a), b_ = (b); a_ > b_ ? a_ : b_; })
#define min(a,b) ({__typeof__(a) a_ = (a), b_ = (b); a_ < b_ ? a_ : b_; })

#ifdef __linux__
#include <endian.h>
#elif defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define be16toh(x) OSSwapBigToHostInt16(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#define htobe16(x) OSSwapHostToBigInt16(x)
#define htobe32(x) OSSwapHostToBigInt32(x)
#define htobe64(x) OSSwapHostToBigInt64(x)
#elif defined(__sparc__) || defined(__sparc)
#define be16toh(x) (x)
#define be32toh(x) (x)
#define be64toh(x) (x)
#define htobe16(x) (x)
#define htobe32(x) (x)
#define htobe64(x) (x)
#endif

// MSG_NOSIGNAL suppresses SIGPIPE on send(), if we don't have it, well,
// let's hope the application set SIGPIPE handler to ignore
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

// clock_gettime is available if _POSIX_TIMERS is defined to >= 1
#if !(defined(_POSIX_TIMERS) && _POSIX_TIMERS >= 1)
#include <sys/time.h>
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif

static __attribute__((unused))
int clock_gettime(int clk_id __attribute__((unused)), struct timespec *t)
{
  struct timeval tv;
  int rv = gettimeofday(&tv, NULL);
  if (rv)
    return rv;
  t->tv_sec = tv.tv_sec;
  t->tv_nsec = tv.tv_usec * 1000;
  return 0;
}
#endif // !_POSIX_TIMERS

// strndup is defined in POSIX.1-2008, GNU had it before, OS X since 10.7
#if !(defined(_GNU_SOURCE) || _POSIX_VERSION >= 200809L || defined(__APPLE__))
#include <stdlib.h>
#include <string.h>

static __attribute__((unused))
char *strndup(const char *s, size_t n)
{
  size_t l = min(n, strlen(s));
  char *r = malloc(l + 1);
  memcpy(r, s, l);
  r[l] = 0;
  return r;
}
#endif // !(_GNU_SOURCE || _POSIX_VERSION >= 200809L || __APPLE__)

#endif // !_OMCACHE_COMPAT_H
