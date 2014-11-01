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

#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

#include "test_omcache.h"

static struct {pid_t pid; unsigned short port; } memcacheds[1000];
static size_t memcached_count;

int ot_start_memcached(const char *addr)
{
  if (memcached_count >= sizeof(memcacheds)/sizeof(memcacheds[0]))
    {
      printf("too many memcacheds running\n");
      return -1;
    }
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  int port = 30000 + (ts.tv_nsec & 0x7fff);
  pid_t pid = fork();
  if (pid == 0)
    {
      const char *mc_path = getenv("MEMCACHED_PATH");
      if (mc_path == NULL)
        mc_path = "/usr/bin/memcached";
      char portbuf[32];
      snprintf(portbuf, sizeof(portbuf), "%d", port);
      printf("Starting %s on port memcached %s\n", mc_path, portbuf);
      execl(mc_path, "memcached", "-vp", portbuf, "-l", addr ? addr : "127.0.0.1", NULL);
      perror("execl");
      _exit(1);
    }
  // XXX: sleep 0.1s to allow memcached to start
  usleep(100000);
  memcacheds[memcached_count].pid = pid;
  memcacheds[memcached_count++].port = port;
  return port;
}

static void kill_memcached(pid_t pid, int port)
{
  printf("Sending SIGTERM to memcached pid %d on port %d\n", (int) pid, port);
  kill(pid, SIGTERM);
}

static void kill_memcacheds(void)
{
  for (size_t i = 0; i < memcached_count; i ++)
    {
      kill_memcached(memcacheds[i].pid, memcacheds[i].port);
    }
}

int ot_stop_memcached(int port)
{
  for (size_t i = 0; i < memcached_count; i ++)
    {
      if (memcacheds[i].port != port)
        continue;
      kill_memcached(memcacheds[i].pid, memcacheds[i].port);
      memmove(&memcacheds[i], &memcacheds[--memcached_count], sizeof(memcacheds[i]));
      return 1;
    }
  return 0;
}

int main(void)
{
  atexit(kill_memcacheds);

  SRunner *sr = srunner_create(NULL);
  for (size_t i = 0; i < sizeof(suites) / sizeof(suites[0]); i ++)
    srunner_add_suite(sr, suites[i]());

  srunner_run_all(sr, CK_NORMAL);
  int number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  return (number_failed == 0) ? 0 : 1;
}
