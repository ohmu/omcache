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

#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "test_omcache.h"
#include "compat.h"

struct mc_info_s {
  pid_t parent_pid;
  pid_t pid;
  unsigned short port;
};

static struct mc_info_s memcacheds[1000];
static size_t memcached_count;
static char memcached_version[100];
static char *memcached_path;

const char *ot_memcached_version(void)
{
  if (memcached_version[0])
    return memcached_version;
  int iop[2];
  pipe(iop);
  pid_t pid = fork();
  if (pid == 0)
    {
      close(0);
      close(1);
      close(2);
      int fd = open("/dev/null", O_RDWR);
      dup2(fd, 0);
      dup2(fd, 2);
      close(fd);
      dup2(iop[1], fileno(stdout));
      close(iop[1]);
      execl(memcached_path, "memcached", "-h", NULL);
      perror("execl");
      _exit(1);
    }
  close(iop[1]);
  read(iop[0], memcached_version, sizeof(memcached_version));
  close(iop[0]);
  memcached_version[sizeof(memcached_version) - 1] = 0;
  char *p = strchr(memcached_version, '\n');
  if (p)
    *p = 0;
  if (memcmp(memcached_version, "memcached ", sizeof("memcached ") - 1) == 0)
    memmove(memcached_version, memcached_version + sizeof("memcached ") - 1, 20);
  return memcached_version;
}

int ot_get_memcached(size_t server_index)
{
  while (server_index >= memcached_count)
    ot_start_memcached(NULL, NULL);
  return memcacheds[server_index].port;
}

int ot_start_memcached(const char *addr, pid_t *pidp)
{
  if (memcached_count >= sizeof(memcacheds)/sizeof(memcacheds[0]))
    {
      printf("too many memcacheds running\n");
      return -1;
    }
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  int port = 30000 + (ts.tv_nsec >> 10 & 0x7fff);
  pid_t pid = fork();
  if (pid == 0)
    {
      char portbuf[32];
      snprintf(portbuf, sizeof(portbuf), "%d", port);
      printf("Starting %s on port memcached %s\n", memcached_path, portbuf);
      execl(memcached_path, "memcached", "-vp", portbuf, "-l", addr ? addr : "127.0.0.1", NULL);
      perror("execl");
      _exit(1);
    }
  // XXX: sleep 0.1s to allow memcached to start
  usleep(100000);
  memcacheds[memcached_count].parent_pid = getpid();
  memcacheds[memcached_count].pid = pid;
  memcacheds[memcached_count++].port = port;
  if (pidp)
    *pidp = pid;
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
    if (memcacheds[i].parent_pid == getpid())
      kill_memcached(memcacheds[i].pid, memcacheds[i].port);
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

omcache_t *ot_init_omcache(int server_count, int log_level)
{
  char srvstr[2048], *p = srvstr;
  omcache_t *oc = omcache_init();
  omcache_set_log_callback(oc, log_level, omcache_log_stderr, NULL);
  if (server_count == 0)
    return oc;
  for (int i = 0; i < server_count; i ++)
    p += sprintf(p, "%s127.0.0.1:%d", i == 0 ? "" : ",", ot_get_memcached(i));
  omcache_set_servers(oc, srvstr);
  return oc;
}

int64_t ot_msec(void)
{
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int main(int argc, char **argv)
{
  atexit(kill_memcacheds);

  // check logs to stdout, omcache to stderr
  setlinebuf(stdout);
  setlinebuf(stderr);

  memcached_path = getenv("MEMCACHED_PATH");
  if (memcached_path == NULL)
    memcached_path = "/usr/bin/memcached";

  // check memcached version (and presence)
  if (ot_memcached_version()[0] == 0)
    {
      fprintf(stderr, "%s: unable to determine memcached version from %s\n"
                      "Set MEMCACHED_PATH environment variable to the right path\n",
                      argv[0], memcached_path);
      return 1;
    }
  printf("memcached version: %s\n", ot_memcached_version());

  // start two memcacheds in the parent process
  ot_start_memcached(NULL, NULL);
  ot_start_memcached(NULL, NULL);

  SRunner *sr = srunner_create(NULL);
  for (size_t i = 0; i < sizeof(suites) / sizeof(suites[0]); i ++)
    srunner_add_suite(sr, suites[i]());

  srunner_set_log(sr, "-");
  if (argc < 2)
    srunner_run_all(sr, CK_VERBOSE);
  else
    srunner_run(sr, NULL, argv[1], CK_VERBOSE);

  int number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);

  return (number_failed == 0) ? 0 : 1;
}
