/*
 * OMcache - a memcached client library
 *
 * Copyright (c) 2013-2014, Oskari Saarenmaa <os@ohmu.fi>
 * All rights reserved.
 *
 * This file is under the Apache License, Version 2.0.
 * See the file `LICENSE` for details.
 *
 */

#include <ctype.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/syslog.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "omcache_priv.h"

#define max(a,b) ({__typeof__(a) a_ = (a), b_ = (b); a_ > b_ ? a_ : b_; })
#define min(a,b) ({__typeof__(a) a_ = (a), b_ = (b); a_ < b_ ? a_ : b_; })


#define omc_log(pri,fmt,...) ({ \
    if (mc->log_cb && pri <= mc->log_level) { \
      char *log_msg_; \
      if (asprintf(&log_msg_, "[%.03f] omcache/%s:%d: " fmt, \
                   (omc_msec() - mc->init_msec) / 1000.0, __func__, __LINE__, __VA_ARGS__) > 0) { \
        mc->log_cb(mc->log_context, (pri), log_msg_); \
        free(log_msg_); \
      } \
    } NULL; })
#define omc_srv_log(pri,srv,fmt,...) \
    omc_log(pri, "[%s:%s] " fmt, (srv)->hostname, (srv)->port, __VA_ARGS__)

#ifndef NDEBUG
#  define omc_debug(...) omc_log(LOG_DEBUG, __VA_ARGS__)
#  define omc_srv_debug(...) omc_srv_log(LOG_DEBUG, __VA_ARGS__)
#else
#  define omc_debug(...)
#  define omc_srv_debug(...)
#endif


typedef struct omc_buf_s
{
  unsigned char *base;
  unsigned char *end;
  unsigned char *r;
  unsigned char *w;
} omc_buf_t;

typedef struct omc_srv_s
{
  int list_index;
  int sock;
  char *hostname;
  char *port;
  struct addrinfo *addrs;
  struct addrinfo *addrp;
  int64_t last_gai;
  int64_t conn_timeout;
  uint32_t last_req_recvd;
  uint32_t last_req_sent;
  uint32_t last_req_sent_nq;
  omc_buf_t send_buffer;
  omc_buf_t recv_buffer;
  bool disabled;
  bool connected;
  int64_t retry_at;
  int64_t last_io_success;
  int64_t last_io_attempt;
  int64_t expected_noop;
} omc_srv_t;

typedef struct omc_ketama_point_s
{
  uint32_t hash_value;
  omc_srv_t *srv;
} omc_ketama_point_t;

typedef struct omc_ketama_s
{
  uint32_t point_count;
  omc_ketama_point_t points[];
} omc_ketama_t;

struct omcache_s
{
  int64_t init_msec;
  uint32_t req_id;
  omc_srv_t **servers;
  struct pollfd *server_polls;
  ssize_t server_count;
  omc_int_hash_table_t *fd_table;

  // distribution
  omc_ketama_t *ketama;
  omcache_dist_t *dist_method;

  // settings
  omcache_log_callback_func *log_cb;
  void *log_context;
  int log_level;

  omcache_response_callback_func *resp_cb;
  void *resp_cb_context;

  size_t recv_buffer_max;
  size_t send_buffer_max;
  uint32_t connect_timeout_msec;
  uint32_t reconnect_timeout_msec;
  uint32_t dead_timeout_msec;
  bool buffer_writes;
};

typedef struct omc_resp_lookup_s
{
  // NOTE: values_returned is the number of responses we've actually been
  // able to store to *resps.  We don't usually have to worry about
  // values_size, but for requests that return more than one response per
  // request (at least stat) we must make sure we don't overflow values.
  omcache_value_t *values;
  size_t values_size;
  size_t values_returned;
  uint32_t req_id_min;
  uint32_t req_id_max;
} omc_resp_lookup_t;

static int omc_srv_free(omcache_t *mc, omc_srv_t *srv);
static int omc_srv_connect(omcache_t *mc, omc_srv_t *srv);
static int omc_srv_io(omcache_t *mc, omc_srv_t *srv, omc_resp_lookup_t *rlookup);
static void omc_srv_reset(omcache_t *mc, omc_srv_t *srv, const char *log_msg);
static int omc_srv_send_noop(omcache_t *mc, omc_srv_t *srv, bool init);
static omc_ketama_t *omc_ketama_create(omcache_t *mc);
static inline int64_t omc_msec();

static int g_iov_max = 0;


omcache_t *omcache_init(void)
{
  if (g_iov_max == 0)
    g_iov_max = sysconf(_SC_IOV_MAX);

  omcache_t *mc = calloc(1, sizeof(*mc));
  mc->init_msec = omc_msec();
  mc->req_id = time(NULL);
  mc->recv_buffer_max = 1024 * (1024 + 32);
  mc->send_buffer_max = 1024 * (1024 * 10);
  mc->connect_timeout_msec = 10 * 1000;
  mc->reconnect_timeout_msec = 10 * 1000;
  mc->dead_timeout_msec = 10 * 1000;
  mc->dist_method = &omcache_dist_libmemcached_ketama;
  return mc;
}

int omcache_free(omcache_t *mc)
{
  if (mc->servers)
    {
      for (off_t i=0; i<mc->server_count; i++)
        {
          if (mc->servers[i])
            {
              omc_srv_free(mc, mc->servers[i]);
              mc->servers[i] = NULL;
            }
        }
      memset(mc->servers, 'L', mc->server_count * sizeof(void *));
      free(mc->servers);
    }
  free(mc->server_polls);
  free(mc->ketama);
  omc_int_hash_table_free(mc->fd_table);
  memset(mc, 'M', sizeof(*mc));
  free(mc);
  return OMCACHE_OK;
}

const char *omcache_strerror(int rc)
{
  switch (rc)
    {
    case OMCACHE_OK: return "Success";
    case OMCACHE_NOT_FOUND: return "Key not found from memcached";
    case OMCACHE_KEY_EXISTS: return "Conflicting key exists in memcached";
    case OMCACHE_TOO_LARGE_VALUE: return "Value size exceeds maximum";
    case OMCACHE_DELTA_BAD_VALUE: return "Existing value can not be incremented or decremented";
    case OMCACHE_FAIL: return "Command failed in memcached";
    case OMCACHE_AGAIN: return "Call would block, try again";
    case OMCACHE_INVALID: return "Invalid parameters";
    case OMCACHE_BUFFERED: return "Data buffered in OMcache";
    case OMCACHE_BUFFER_FULL: return "Buffer full, command dropped";
    case OMCACHE_NO_SERVERS: return "No server available";
    case OMCACHE_SERVER_FAILURE: return "Failure communicating to server";
    default: return "Unknown";
    }
}

static int omc_map_mc_status_to_ret_code(protocol_binary_response_status status)
{
  switch (status)
    {
    case PROTOCOL_BINARY_RESPONSE_SUCCESS: return OMCACHE_OK;
    case PROTOCOL_BINARY_RESPONSE_KEY_ENOENT: return OMCACHE_NOT_FOUND;
    case PROTOCOL_BINARY_RESPONSE_KEY_EEXISTS: return OMCACHE_KEY_EXISTS;
    case PROTOCOL_BINARY_RESPONSE_E2BIG: return OMCACHE_TOO_LARGE_VALUE;
    case PROTOCOL_BINARY_RESPONSE_DELTA_BADVAL: return OMCACHE_DELTA_BAD_VALUE;
    default:
      return OMCACHE_FAIL;
    }
}

void omcache_log_stderr(void *context,
                        int level,
                        const char *msg)
{
  fprintf(stderr, "%s%s %s\n",
          context ? (const char *) context : "",
          level == LOG_ERR ? "ERROR" :
          level == LOG_WARNING ? "WARNING" :
          level == LOG_NOTICE ? "NOTICE" :
          level == LOG_INFO ? "INFO" :
          level == LOG_DEBUG ? "DEBUG" :
          "?", msg);
}

static inline int64_t omc_msec()
{
  struct timespec ts;
#ifdef CLOCK_MONOTONIC_COARSE
  if (clock_gettime(CLOCK_MONOTONIC_COARSE, &ts) == -1)
#endif
    clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static omc_srv_t *omc_srv_init(const char *hostname)
{
  const char *p;
  omc_srv_t *srv = calloc(1, sizeof(*srv));
  srv->sock = -1;
  srv->list_index = -1;
  if (*hostname == '[' && (p = strchr(hostname, ']')) != NULL)
    {
      // handle [addr]:port form
      srv->hostname = strndup(hostname + 1, p - (hostname + 1));
      if (*(p + 1) == ':')
        srv->port = strdup(p + 2);
      else
        srv->port = strdup(MC_PORT);
    }
  else if ((p = strchr(hostname, ':')) != NULL)
    {
      // handle hostname:port form
      srv->hostname = strndup(hostname, p - hostname);
      srv->port = strdup(p + 1);
    }
  else
    {
      // just use hostname as-is and default MC port
      srv->hostname = strdup(hostname);
      srv->port = strdup(MC_PORT);
    }
  return srv;
}

static int omc_srv_free(omcache_t *mc, omc_srv_t *srv)
{
  if (srv->sock >= 0)
    {
      shutdown(srv->sock, SHUT_RDWR);
      close(srv->sock);
      omc_int_hash_table_del(mc->fd_table, srv->sock);
      srv->sock = -1;
    }
  if (srv->addrs)
    freeaddrinfo(srv->addrs);
  free(srv->send_buffer.base);
  free(srv->recv_buffer.base);
  free(srv->hostname);
  free(srv->port);
  memset(srv, 'S', sizeof(*srv));
  free(srv);
  return OMCACHE_OK;
}

static int omc_srv_cmp(const omc_srv_t *s1, const omc_srv_t *s2)
{
  int res = strcmp(s1->hostname, s2->hostname);
  if (res != 0)
    return res;
  return strcmp(s1->port, s2->port);
}

static int omc_srvp_cmp(const void *sp1, const void *sp2)
{
  return omc_srv_cmp(*(omc_srv_t * const *) sp1, *(omc_srv_t * const *) sp2);
}

int omcache_set_servers(omcache_t *mc, const char *servers)
{
  omc_srv_t **srv_new = NULL;
  ssize_t srv_new_count = 0, srv_new_size = 0, srv_len;
  char *srv_dup = strdup(servers), *srv, *p;

  // parse and sort comma-delimited list of servers and strip whitespace
  for (srv=srv_dup; srv; srv=p)
    {
      p = strchr(srv, ',');
      if (p)
        *p++ = 0;
      while (*srv && isspace(*srv))
        srv++;
      srv_len = strlen(srv);
      while (srv_len > 0 && isspace(srv[srv_len-1]))
        srv[--srv_len] = 0;
      if (srv_len == 0)
        continue;
      if (srv_new_count >= srv_new_size)
        {
          srv_new_size += 16;
          srv_new = realloc(srv_new, sizeof(*srv_new) * srv_new_size);
        }
      srv_new[srv_new_count++] = omc_srv_init(srv);
    }
  free(srv_dup);

  qsort(srv_new, srv_new_count, sizeof(*srv_new), omc_srvp_cmp);

  // preallocated poll-structures for all servers
  if (mc->server_count != srv_new_count)
    mc->server_polls = realloc(mc->server_polls, srv_new_count * sizeof(*mc->server_polls));

  // remove old servers that weren't on the new list and add the new ones
  if (mc->server_count)
    {
      ssize_t i=0, j=0;
      while (i < mc->server_count)
        {
          int res = (j >= srv_new_count) ? -1 : omc_srv_cmp(mc->servers[i], srv_new[j]);
          if (res == 0)
            {
              // the same server is on both lists:
              // move it to the new list
              omc_srv_free(mc, srv_new[j]);
              srv_new[j++] = mc->servers[i++];
            }
          else if (res < 0)
            {
              // server on old list is before the server on new list:
              // it's not on the new list at all
              omc_srv_free(mc, mc->servers[i++]);
            }
          else if (res > 0)
            {
              // server on old list is after the server on new list:
              // it may be included on the new list as well
              j++;
            }
        }
      free(mc->servers);
    }

  mc->servers = srv_new;
  mc->server_count = srv_new_count;

  // reset list indices and fd_table
  mc->fd_table = omc_int_hash_table_init(mc->fd_table, mc->server_count);
  for (ssize_t i=0; i<mc->server_count; i++)
    {
      omc_srv_debug(mc->servers[i], "server #%zd", i);
      mc->servers[i]->list_index = i;
      if (mc->servers[i]->sock >= 0)
        omc_int_hash_table_add(mc->fd_table, mc->servers[i]->sock, i);
    }

  // rerun distribution
  free(mc->ketama);
  mc->ketama = omc_ketama_create(mc);
  return OMCACHE_OK;
}

int omcache_set_distribution_method(omcache_t *mc, omcache_dist_t *method)
{
  mc->dist_method = method;
  // rerun distribution
  free(mc->ketama);
  mc->ketama = omc_ketama_create(mc);
  return OMCACHE_OK;
}

int omcache_set_log_callback(omcache_t *mc, int level, omcache_log_callback_func *func, void *context)
{
  mc->log_cb = func;
  mc->log_context = context;
  mc->log_level = level ? level : LOG_DEBUG - 1;
  return OMCACHE_OK;
}

int omcache_set_connect_timeout(omcache_t *mc, uint32_t msec)
{
  mc->connect_timeout_msec = msec;
  return OMCACHE_OK;
}

int omcache_set_reconnect_timeout(omcache_t *mc, uint32_t msec)
{
  mc->reconnect_timeout_msec = msec;
  return OMCACHE_OK;
}

int omcache_set_dead_timeout(omcache_t *mc, uint32_t msec)
{
  mc->dead_timeout_msec = msec;
  return OMCACHE_OK;
}

int omcache_set_recv_buffer_max_size(omcache_t *mc, size_t size)
{
  mc->recv_buffer_max = size;
  return OMCACHE_OK;
}

int omcache_set_send_buffer_max_size(omcache_t *mc, size_t size)
{
  mc->send_buffer_max = size;
  return OMCACHE_OK;
}

int omcache_set_response_callback(omcache_t *mc, omcache_response_callback_func *resp_cb, void *resp_cb_context)
{
  mc->resp_cb = resp_cb;
  mc->resp_cb_context = resp_cb_context;
  return OMCACHE_OK;
}

struct pollfd *omcache_poll_fds(omcache_t *mc, int *nfds, int *poll_timeout)
{
  int n, i;
  int64_t now = omc_msec();
  *poll_timeout = mc->dead_timeout_msec;

  for (i=n=0; i<mc->server_count; i++)
    {
      omc_srv_t *srv = mc->servers[i];
      mc->server_polls[n].events = 0;
      if (srv->last_req_sent != srv->last_req_sent_nq)
        {
          omc_srv_send_noop(mc, srv, false);
        }
      if (srv->last_req_recvd < srv->last_req_sent_nq)
        {
          omc_srv_debug(srv, "polling %d for POLLIN", srv->sock);
          mc->server_polls[n].events |= POLLIN;
        }
      if (srv->send_buffer.w != srv->send_buffer.r || srv->conn_timeout > 0)
        {
          omc_srv_debug(srv, "polling %d for POLLOUT", srv->sock);
          mc->server_polls[n].events |= POLLOUT;
        }
      if (mc->server_polls[n].events != 0)
        {
          if (srv->sock < 0)
            omc_srv_connect(mc, srv);
          // make sure poll timeout is at connection timeout, and in
          // case it has already expired, set connection timeout to a
          // special value (1) so next time we get here we know we
          // weren't able to establish a connection in time.
          if (srv->conn_timeout == 1)
            omc_srv_reset(mc, srv, "timeout waiting for connect");
          else if (srv->conn_timeout > 0 && now < srv->conn_timeout)
            *poll_timeout = min(*poll_timeout, srv->conn_timeout - now);
          else if (srv->conn_timeout > 0)
            {
              srv->conn_timeout = 1;
              *poll_timeout = 1;
            }
          if (srv->sock < 0)
            continue;
          mc->server_polls[n].fd = srv->sock;
          mc->server_polls[n].revents = 0;
          n ++;
        }
    }
  *nfds = n;
  return mc->server_polls;
}

static int omc_ketama_point_cmp(const void *v1, const void *v2)
{
  const omc_ketama_point_t *p1 = v1, *p2 = v2;
  if (p1->hash_value == p2->hash_value)
    return 0;
  return p1->hash_value > p2->hash_value ? 1 : -1;
}

static omc_ketama_t *omc_ketama_create(omcache_t *mc)
{
  uint32_t pps = mc->dist_method->points_per_server;
  uint32_t eps = mc->dist_method->entries_per_point;
  size_t cidx = 0, total_points = mc->server_count * pps * eps;
  omc_ketama_t *ktm = (omc_ketama_t *) malloc(sizeof(omc_ketama_t) + total_points * sizeof(omc_ketama_point_t));

  for (ssize_t i = 0; i < mc->server_count; i ++)
    {
      omc_srv_t *srv = mc->servers[i];
      uint32_t hashes[eps];
      for (size_t p = 0; p < pps; p ++)
        {
          uint32_t sp_count = mc->dist_method->point_hash_func(srv->hostname, srv->port, p, hashes);
          for (uint32_t e = 0; e < sp_count; e ++)
            ktm->points[cidx++] = (omc_ketama_point_t) { .srv = srv, .hash_value = hashes[e] };
        }
    }

  ktm->point_count = cidx;
  qsort(ktm->points, ktm->point_count, sizeof(omc_ketama_point_t), omc_ketama_point_cmp);

  return ktm;
}

static int omc_ketama_lookup(omcache_t *mc, const unsigned char *key, size_t key_len)
{
  uint32_t hash_value = mc->dist_method->key_hash_func(key, key_len);
  const omc_ketama_point_t *first = mc->ketama->points, *last = mc->ketama->points + mc->ketama->point_count;
  const omc_ketama_point_t *left = first, *right = last, *selected;
  bool wrap = false;
  int64_t now = 0;

  while (left < right)
    {
      const omc_ketama_point_t *middle = left + (right - left) / 2;
      if (middle->hash_value < hash_value)
        left = middle + 1;
      else
        right = middle;
    }

  // skip disabled servers
  size_t skipped = 0;
  for (selected = (right == last) ? first : right;
      selected == last || selected->srv->disabled;
      selected++)
    {
      if (selected != last)
        {
          // try to bring disabled servers back online but don't select them
          // yet as we need to (asynchronously) verify that they're usable
          if (selected->srv->disabled)
            {
              if (now == 0)
                now = omc_msec();
              // only attempt io with servers once per millisecond (or once
              // per API call) unless there's some progress in the connection
              if (now > selected->srv->retry_at &&
                  max(now, selected->srv->last_io_success + 1) > selected->srv->last_io_attempt)
                {
                  omc_srv_io(mc, selected->srv, NULL);
                }
            }
          skipped ++;
          continue;
        }
      if (wrap)
        {
          omc_log(LOG_ERR, "%s", "all servers are disabled");
          return -1;
        }
      wrap = true;
      selected = first;
    }
  if (skipped)
    omc_log(LOG_INFO, "ketama skipped %zu disabled server points", skipped);
  return selected->srv->list_index;
}

int omcache_server_index_for_key(omcache_t *mc, const unsigned char *key, size_t key_len)
{
  if (mc->server_count > 1)
    return omc_ketama_lookup(mc, key, key_len);
  return 0;
}

static void omc_srv_disable(omcache_t *mc, omc_srv_t *srv)
{
  // disable server until reconnect timeout
  // log at higher level if it's not yet disabled
  omc_srv_log(srv->disabled ? LOG_INFO : LOG_NOTICE, srv,
              "disabling server for %u msec", mc->reconnect_timeout_msec);
  srv->retry_at = omc_msec() + mc->reconnect_timeout_msec;
  srv->disabled = true;
  // clear addrinfo cache to force fresh addrs to be used on retry
  if (srv->addrs)
    freeaddrinfo(srv->addrs);
  srv->addrs = NULL;
  srv->addrp = NULL;
}

static void
omc_srv_reset(omcache_t *mc, omc_srv_t *srv, const char *log_msg)
{
  omc_srv_log(LOG_NOTICE, srv, "reset: %s (%s)", log_msg, strerror(errno));
  if (srv->sock != -1)
    {
      close(srv->sock);
      omc_int_hash_table_del(mc->fd_table, srv->sock);
    }
  srv->connected = false;
  srv->sock = -1;
  srv->conn_timeout = 0;
  srv->last_req_recvd = 0;
  srv->last_req_sent = 0;
  srv->last_req_sent_nq = 0;
  srv->recv_buffer.r = srv->recv_buffer.base;
  srv->recv_buffer.w = srv->recv_buffer.base;
  srv->send_buffer.r = srv->send_buffer.base;
  srv->send_buffer.w = srv->send_buffer.base;
  if (srv->expected_noop)
    {
      omc_srv_disable(mc, srv);
    }
}

// make sure a connection is established to the server, if not, try to set it up
static int omc_srv_connect(omcache_t *mc, omc_srv_t *srv)
{
  if (srv->connected)
    {
      return OMCACHE_OK;  // all is good
    }
  int64_t now = omc_msec();
  if (srv->disabled && now < srv->retry_at)
    {
      return OMCACHE_NO_SERVERS;
    }
  if (srv->sock == -1)
    {
      int err;

      // refresh the hosts addresses if we don't have them yet or if we last
      // refreshed them before we were last connected
      if (srv->addrs == NULL || srv->last_io_success > srv->last_gai)
        {
          if (srv->addrs)
            {
              freeaddrinfo(srv->addrs);
              srv->addrs = NULL;
            }
          struct addrinfo hints;
          memset(&hints, 0, sizeof(hints));
          hints.ai_socktype = SOCK_STREAM;
          hints.ai_flags = AI_ADDRCONFIG;
          // XXX: getaddrinfo blocks, use libares-c or something?
          // getaddrinfo_a isn't really usable.
          // alternatively just document this and let the calling
          // application handle name resolution before in whatever async way
          // it likes before handing the host to us.
          err = getaddrinfo(srv->hostname, srv->port, &hints, &srv->addrs);
          if (err != 0)
            {
              omc_srv_log(LOG_WARNING, srv, "getaddrinfo: %s", gai_strerror(err));
              omc_srv_reset(mc, srv, "getaddrinfo failed");
              omc_srv_disable(mc, srv);
              return OMCACHE_SERVER_FAILURE;
            }
          srv->addrp = srv->addrs;
          srv->last_gai = now;
        }
      while (srv->addrp)
        {
          int sock = socket(srv->addrp->ai_family, srv->addrp->ai_socktype, srv->addrp->ai_protocol);
          if (sock < 0)
            {
              omc_srv_reset(mc, srv, "socket creation failed");
            }
          else if (fcntl(sock, F_SETFL, O_NONBLOCK) < 0)
            {
              omc_srv_reset(mc, srv, "fcntl(sock, F_SETFL, O_NONBLOCK)");
              close(sock);
              sock = -1;
            }
          if (sock < 0)
            {
              omc_srv_disable(mc, srv);
              return OMCACHE_SERVER_FAILURE;
            }
          omc_int_hash_table_add(mc->fd_table, sock, srv->list_index);
          err = connect(sock, srv->addrp->ai_addr, srv->addrp->ai_addrlen);
          srv->last_io_attempt = now;
          srv->addrp = srv->addrp->ai_next;
          srv->sock = sock;
          if (err == 0)
            {
              // connection established
              break;
            }
          else if (errno == EINPROGRESS)
            {
              srv->conn_timeout = now + mc->connect_timeout_msec;
              omc_srv_debug(srv, "%s", "connection in progress");
              return OMCACHE_AGAIN;
            }
          else
            {
              omc_srv_reset(mc, srv, "connect failed");
            }
        }
      if (srv->sock == -1)
        {
          omc_srv_reset(mc, srv, "no connection established");
          // disable server if we've walked through the server list and
          // weren't able to conncet to any address
          if (srv->addrp == NULL)
            omc_srv_disable(mc, srv);
          return OMCACHE_SERVER_FAILURE;
        }
    }
  else if (srv->conn_timeout > 0)
    {
      struct pollfd pfd = { .fd = srv->sock, .events = POLLOUT, .revents = 0, };
      if (poll(&pfd, 1, 0) == 0)
        {
          if (omc_msec() >= srv->conn_timeout)
            {
              // timeout
              errno = EAGAIN;
              omc_srv_reset(mc, srv, "connection timeout");
            }
          return OMCACHE_AGAIN;
        }
      int err;
      socklen_t err_len = sizeof(err);
      if (getsockopt(srv->sock, SOL_SOCKET, SO_ERROR, &err, &err_len) == -1)
        {
          omc_srv_log(LOG_WARNING, srv, "getsockopt failed: %s", strerror(errno));
          err = errno;
        }
      if (err)
        {
          errno = err;
          omc_srv_reset(mc, srv, "async connect failed");
          return OMCACHE_AGAIN;
        }
    }
  srv->connected = true;
  srv->conn_timeout = 0;
  srv->last_io_success = omc_msec();
  omc_srv_log(LOG_INFO, srv, "%s", "connected");
  omc_srv_send_noop(mc, srv, true);
  return OMCACHE_OK;
}

static int omc_buffer_realloc(omc_buf_t *buf, size_t buf_max, uint32_t required)
{
  size_t space = buf->end - buf->w, buffered = buf->w - buf->r;

  if (space >= required)
    return OMCACHE_OK;

  // move the contents of the buffer to its beginning
  if (buf->r != buf->base)
    {
      memmove(buf->base, buf->r, buffered);
      space = buf->end - buf->base - buffered;
    }

  // allocate more space if required
  if (space < required)
    {
      if ((size_t) (buf->end - buf->base) + required > buf_max)
        return OMCACHE_BUFFER_FULL;
      size_t new_size = min(buf_max, buffered + required + 30000);
      buf->base = realloc(buf->base, new_size);
      buf->end = buf->base + new_size;
    }

  buf->r = buf->base;
  buf->w = buf->base + buffered;
  return OMCACHE_OK;
}

static int omc_do_read(omcache_t *mc, omc_srv_t *srv, size_t msg_size, bool keep_buffers)
{
  // make sure we have room for at least the requested bytes, but read as much as possible
  size_t space = srv->recv_buffer.end - srv->recv_buffer.w;
  if (space < msg_size)
    {
      if (keep_buffers)
        return OMCACHE_BUFFER_FULL;
      if (omc_buffer_realloc(&srv->recv_buffer, mc->recv_buffer_max, msg_size) != OMCACHE_OK)
        return OMCACHE_BUFFER_FULL;
      space = srv->recv_buffer.end - srv->recv_buffer.w;
    }
  ssize_t res = read(srv->sock, srv->recv_buffer.w, space);
  srv->last_io_attempt = omc_msec();
  if (res <= 0 && errno != EINTR && errno != EAGAIN)
    {
      omc_srv_reset(mc, srv, "read failed");
      return OMCACHE_SERVER_FAILURE;
    }
  omc_srv_debug(srv, "read %zd bytes to a buffer of %zu bytes %s",
                res, space, (res == -1) ? strerror(errno) : "");
  if (res <= 0)
    return OMCACHE_AGAIN;
  srv->recv_buffer.w += res;
  srv->last_io_success = srv->last_io_attempt;
  return OMCACHE_OK;
}

// read any responses returned by the server calling mc->resp_cb on them.
// if a response's 'opaque' matches req_id store that response in *resp.
static int omc_srv_read(omcache_t *mc, omc_srv_t *srv, omc_resp_lookup_t *rlookup)
{
  protocol_binary_response_header stack_header;
  bool keep_buffers = false;
  int ret = OMCACHE_OK;

  // handle as many messages as possible
  for (int i=0; ret == OMCACHE_OK; i++)
    {
      if (i == 0 || srv->recv_buffer.r == srv->recv_buffer.w)
        {
          ret = omc_do_read(mc, srv, 255, keep_buffers);
          continue;
        }

      omcache_value_t value = {0};
      size_t buffered = srv->recv_buffer.w - srv->recv_buffer.r;
      size_t msg_size = sizeof(protocol_binary_response_header);
      if (buffered < msg_size)
        {
          omc_srv_debug(srv, "msg %d: not enough data in buffer (%zd, need %zd)",
                        i, buffered, msg_size);
          break;
        }
      protocol_binary_response_header *hdr =
        (protocol_binary_response_header *) srv->recv_buffer.r;
      // make sure the message is properly aligned
      if (((unsigned long) (void *) hdr) % 8)
        {
          memcpy(&stack_header, srv->recv_buffer.r, msg_size);
          hdr = &stack_header;
        }
      if (hdr->response.magic != PROTOCOL_BINARY_RES)
        {
          errno = EINVAL;
          omc_srv_reset(mc, srv, "header");
          break;
        }
      // check body length (but don't overwrite it in the buffer yet)
      size_t body_size = be32toh(hdr->response.bodylen);
      msg_size += body_size;
      if (buffered < msg_size)
        {
          omc_srv_debug(srv, "msg %d: not enough data in buffer (%zd, need %zd)",
                        i, buffered, msg_size);
          // read more data if possible, in case we're asked to not move the
          // buffer this may fail and the caller needs to try again later.
          ret = omc_do_read(mc, srv, msg_size - buffered, keep_buffers);
          if (ret == OMCACHE_BUFFER_FULL && !keep_buffers)
            {
              // the message is too big to fit in our receive buffer at all,
              // discard it (by resetting connection.)
              memset(&value, 0, sizeof(value));
              value.status = OMCACHE_BUFFER_FULL;
              if (buffered >= sizeof(hdr->bytes) + hdr->response.extlen + be16toh(hdr->response.keylen))
                {
                  value.key = srv->recv_buffer.r + sizeof(hdr->bytes) + hdr->response.extlen;
                  value.key_len = be16toh(hdr->response.keylen);
                }
              if (mc->resp_cb)
                mc->resp_cb(mc, &value, mc->resp_cb_context);
              if (rlookup &&
                  rlookup->values_size > rlookup->values_returned &&
                  hdr->response.opaque >= rlookup->req_id_min &&
                  hdr->response.opaque <= rlookup->req_id_max)
                {
                  rlookup->values[rlookup->values_returned++] = value;
                }
              errno = EMSGSIZE;
              omc_srv_reset(mc, srv, "buffer full - can't handle response");
            }
          continue;
        }

      omc_srv_debug(srv, "received message: type 0x%hhx, status 0x%hx, id %u",
                    hdr->response.opcode, be16toh(hdr->response.status),
                    hdr->response.opaque);

      if (hdr->response.opaque)
        {
          if (!(hdr->response.opcode == PROTOCOL_BINARY_CMD_STAT &&
                hdr->response.status == 0 &&
                hdr->response.keylen != 0))
            {
              // set last received request number for everything but a
              // successful response to stat request which doesn't have an
              // empty key which signals end of stat responses.
              srv->last_req_recvd = hdr->response.opaque;
            }
          if (hdr->response.opcode == PROTOCOL_BINARY_CMD_NOOP &&
              hdr->response.opaque == srv->expected_noop)
            {
              // a connection setup noop message, mark server alive and don't process this further.
              srv->expected_noop = 0;
              if (srv->disabled)
                {
                  omc_srv_log(LOG_NOTICE, srv, "%s", "re-enabling server");
                  srv->disabled = false;
                }
              srv->recv_buffer.r += msg_size;
              omc_srv_debug(srv, "%s", "received expected noop packet");
              continue;
            }
        }

      // setup response object
      value.status = omc_map_mc_status_to_ret_code(be16toh(hdr->response.status));
      value.key = srv->recv_buffer.r + sizeof(hdr->bytes) + hdr->response.extlen;
      value.key_len = be16toh(hdr->response.keylen);
      value.data = value.key + value.key_len;
      value.data_len = be32toh(hdr->response.bodylen) - hdr->response.extlen - value.key_len;
      value.cas = be64toh(hdr->response.cas);

      if (hdr->response.opcode == PROTOCOL_BINARY_CMD_GET ||
          hdr->response.opcode == PROTOCOL_BINARY_CMD_GETQ ||
          hdr->response.opcode == PROTOCOL_BINARY_CMD_GETK ||
          hdr->response.opcode == PROTOCOL_BINARY_CMD_GETKQ)
        {
          // don't cast recv_buffer to protocol_binary_response_header as
          // that'd require us to realign it properly and in practice we'll
          // be swapping bytes anyway so may as well do it manually
          const unsigned char *b = srv->recv_buffer.r + sizeof(*hdr);
          memcpy(&value.flags, b, 4);
          value.flags = be32toh(value.flags);
        }

      if (hdr->response.opcode == PROTOCOL_BINARY_CMD_INCREMENT ||
          hdr->response.opcode == PROTOCOL_BINARY_CMD_DECREMENT ||
          hdr->response.opcode == PROTOCOL_BINARY_CMD_INCREMENTQ ||
          hdr->response.opcode == PROTOCOL_BINARY_CMD_DECREMENTQ)
        {
          const unsigned char *b = srv->recv_buffer.r + sizeof(*hdr);
          memcpy(&value.delta_value, b, 8);
          value.delta_value = be64toh(value.delta_value);
        }

      srv->recv_buffer.r += msg_size;

      // pass it to response callback
      if (mc->resp_cb)
        {
          mc->resp_cb(mc, &value, mc->resp_cb_context);
        }

      // return if it it was requested and we have room for the response
      if (rlookup &&
          hdr->response.opaque >= rlookup->req_id_min &&
          hdr->response.opaque <= rlookup->req_id_max)
        {
          omc_srv_debug(srv, "got expected response %u (%u / %u)",
                        hdr->response.opaque, hdr->response.opaque - rlookup->req_id_min,
                        rlookup->req_id_max - rlookup->req_id_min + 1);
          if (rlookup->values_size <= rlookup->values_returned)
            {
              if (rlookup->values_size)
                omc_srv_log(LOG_WARNING, srv,
                            "no space to store response %u "
                            "in a buffer of %zu entries, dropping response",
                            hdr->response.opaque, rlookup->values_size);
              continue;
            }
          rlookup->values[rlookup->values_returned++] = value;
          // don't overwrite the buffer to avoid overwriting the response
          keep_buffers = true;
        }
    }
  // reset read buffer in case everything was processed
  if (srv->recv_buffer.r == srv->recv_buffer.w)
    {
      srv->recv_buffer.r = srv->recv_buffer.base;
      srv->recv_buffer.w = srv->recv_buffer.base;
    }
  if (srv->last_req_recvd >= srv->last_req_sent_nq)
    return OMCACHE_OK;
  return OMCACHE_AGAIN;
}

// try to write/connect if there's pending data to this server.  read any
// responses returned by the server calling mc->resp_cb on them.  if a
// response's 'opaque' matches req_id store that response in *resp.
static int omc_srv_io(omcache_t *mc, omc_srv_t *srv, omc_resp_lookup_t *rlookup)
{
  bool again_w = false, again_r = false;
  ssize_t buf_len = srv->send_buffer.w - srv->send_buffer.r;

  int ret = omc_srv_connect(mc, srv);
  if (ret != OMCACHE_OK)
    return ret;

  // recalculate buffer length, omc_srv_connect may have modified it
  buf_len = srv->send_buffer.w - srv->send_buffer.r;
  if (buf_len > 0)
    {
      ssize_t res = write(srv->sock, srv->send_buffer.r, buf_len);
      srv->last_io_attempt = omc_msec();
      if (res <= 0)
        {
          if (errno != EINTR && errno != EAGAIN)
            omc_srv_reset(mc, srv, "write failed");
          // if write to a disabled server (ie on reconnect) would block and
          // we haven't been able to write for dead_timeout msec we'll kill
          // the connection and disable the server for a while more
          if (errno == EAGAIN && srv->disabled &&
              srv->last_io_attempt - srv->last_io_success >= mc->dead_timeout_msec)
            omc_srv_reset(mc, srv, "write timed out");
          return OMCACHE_SERVER_FAILURE;
        }
      omc_srv_debug(srv, "write %zd bytes of %zd bytes %s",
                    res, buf_len, (res == -1) ? strerror(errno) : "");
      srv->last_io_success = srv->last_io_attempt;
      srv->send_buffer.r += res;
      buf_len -= res;
      // reset send buffer in case everything was written
      if (buf_len > 0)
        {
          again_w = true;
        }
      else
        {
          srv->send_buffer.r = srv->send_buffer.base;
          srv->send_buffer.w = srv->send_buffer.base;
        }
    }
  if (srv->conn_timeout == 0 && srv->sock >= 0 &&
      srv->last_req_recvd < srv->last_req_sent_nq)
    {
      ret = omc_srv_read(mc, srv, rlookup);
      if (ret == OMCACHE_AGAIN)
        again_r = true;
      else if (ret != OMCACHE_OK)
        return ret;
    }
  return (again_w || again_r) ? OMCACHE_AGAIN : OMCACHE_OK;
}

// Process writes and reads until we see a response to req_id or until
// timeout_msec has passed.
// If reqs are given the relevant responses will be stored in values.
int omcache_io(omcache_t *mc,
               omcache_req_t *reqs, size_t *req_count,
               omcache_value_t *values, size_t *value_count,
               int32_t timeout_msec)
{
  int again = 0, ret = OMCACHE_OK;
  bool nothing_to_poll = false;
  int64_t now = omc_msec();
  int64_t timeout_abs = (timeout_msec > 0) ? now + timeout_msec : - 1;
  omc_resp_lookup_t rlookup = {0};

  if (reqs && req_count && *req_count)
    {
      rlookup.values = values;
      rlookup.values_size = value_count ? *value_count : 0;
      rlookup.req_id_min = reqs[0].header.opaque;
      rlookup.req_id_max = reqs[*req_count - 1].header.opaque;
      if (rlookup.req_id_max - rlookup.req_id_min + 1 != *req_count)
        return OMCACHE_INVALID;
      if (value_count && *value_count < *req_count)
        return OMCACHE_INVALID;
    }
  if (value_count)
    *value_count = 0;

  while (ret == OMCACHE_OK || ret == OMCACHE_AGAIN)
    {
      omc_debug("looking for req_ids (%u..%u); timeout in %lld msec",
                rlookup.req_id_min, rlookup.req_id_max,
                (long long) (timeout_abs != -1 ? timeout_abs - omc_msec() : -1));
      if (timeout_abs >= 0)
        {
          now = omc_msec();
          if (now > timeout_abs)
            {
              omc_debug("%s", "omcache_io timeout");
              ret = OMCACHE_AGAIN;
              break;
            }
          timeout_msec = timeout_abs - now;
        }

      again = 0;
      int nfds = -1, timeout_poll = -1, polls __attribute__((unused)) = -1;
      struct pollfd *pfds = omcache_poll_fds(mc, &nfds, &timeout_poll);
      if (nfds == 0)
        {
          omc_debug("%s", "nothing to poll, breaking");
          ret = OMCACHE_OK;
          nothing_to_poll = true;
          break;
        }
      timeout_poll = (timeout_msec > 0) ? min(timeout_msec, timeout_poll) : timeout_poll;
      polls = poll(pfds, nfds, timeout_poll);
      omc_debug("poll(%d, %d): %d %s", nfds, timeout_poll, polls, polls == -1 ? strerror(errno) : "");
      now = omc_msec();
      for (int i=0; i<nfds; i++)
        {
          int server_index = omc_int_hash_table_find(mc->fd_table, pfds[i].fd);
          if (server_index == -1)
            {
              omc_log(LOG_ERR, "server socket %d not found from fd_table!", pfds[i].fd);
              abort();
            }
          omc_srv_t *srv = mc->servers[server_index];
          if (srv->sock != pfds[i].fd)
            {
              omc_srv_log(LOG_ERR, srv, "server socket %d does not match poll fd %d!", srv->sock, pfds[i].fd);
              abort();
            }
          if (!pfds[i].revents)
            {
              // reset connections that have failed
              if (now - srv->last_io_attempt >= mc->dead_timeout_msec)
                {
                  errno = ETIMEDOUT;
                  omc_srv_reset(mc, srv, "io timeout");
                  again ++;
                }
              continue;
            }
          ret = omc_srv_io(mc, srv, &rlookup);
          omc_srv_debug(srv, "io: %s", omcache_strerror(ret));
          if (ret == OMCACHE_AGAIN)
            again ++;
          else if (ret != OMCACHE_OK)
            break;
        }

      omc_debug("status %d (%s), %zu responses found, timeout %d",
                ret, omcache_strerror(ret), rlookup.values_returned, timeout_msec);

      // break the loop if we found any responses: the receive buffer could be
      // overwritten by new calls to omc_srv_io so we need to allow the
      // caller to process the response before resuming reads.
      if (rlookup.values_returned)
        {
          // don't want to return OMCACHE_AGAIN if we found our key
          omc_debug("found %zu responses, breaking loop", rlookup.values_returned);
          ret = OMCACHE_OK;
          again = 0;
          break;
        }

      // break the loop in case we didn't want to poll
      if (timeout_msec == 0)
        break;
    }
  if (nothing_to_poll && req_count)
    *req_count = 0;
  if (value_count)
    *value_count = rlookup.values_returned;
  if (nothing_to_poll)
    return OMCACHE_OK;
  if (ret == OMCACHE_OK && again > 0)
    return OMCACHE_AGAIN;
  return ret;
}

int omcache_set_buffering(omcache_t *mc, uint32_t enabled)
{
  mc->buffer_writes = enabled ? true : false;
  return OMCACHE_OK;
}

int omcache_reset_buffers(omcache_t *mc)
{
  for (off_t i=0; i<mc->server_count; i++)
    {
      omc_srv_t *srv = mc->servers[i];
      srv->send_buffer.r = srv->send_buffer.base;
      srv->send_buffer.w = srv->send_buffer.base;
      srv->recv_buffer.r = srv->recv_buffer.base;
      srv->recv_buffer.w = srv->recv_buffer.base;
      srv->last_req_recvd = srv->last_req_sent;
      srv->last_req_sent_nq = srv->last_req_sent;
    }
  return OMCACHE_OK;
}

static bool omc_is_request_quiet(uint8_t opcode)
{
  switch (opcode)
    {
    case PROTOCOL_BINARY_CMD_GETQ:
    case PROTOCOL_BINARY_CMD_GETKQ:
    case PROTOCOL_BINARY_CMD_SETQ:
    case PROTOCOL_BINARY_CMD_ADDQ:
    case PROTOCOL_BINARY_CMD_REPLACEQ:
    case PROTOCOL_BINARY_CMD_DELETEQ:
    case PROTOCOL_BINARY_CMD_INCREMENTQ:
    case PROTOCOL_BINARY_CMD_DECREMENTQ:
    case PROTOCOL_BINARY_CMD_QUITQ:
    case PROTOCOL_BINARY_CMD_FLUSHQ:
    case PROTOCOL_BINARY_CMD_APPENDQ:
    case PROTOCOL_BINARY_CMD_PREPENDQ:
    case PROTOCOL_BINARY_CMD_GATQ:
    case PROTOCOL_BINARY_CMD_GATKQ:
    case PROTOCOL_BINARY_CMD_RSETQ:
    case PROTOCOL_BINARY_CMD_RAPPENDQ:
    case PROTOCOL_BINARY_CMD_RPREPENDQ:
    case PROTOCOL_BINARY_CMD_RDELETEQ:
    case PROTOCOL_BINARY_CMD_RINCRQ:
    case PROTOCOL_BINARY_CMD_RDECRQ:
      return true;
    }
  return false;
}

static int omc_srv_submit(omcache_t *mc, omc_srv_t *srv,
                          struct iovec *iov, size_t iov_cnt,
                          size_t req_cnt __attribute__((unused)),
                          struct omcache_req_header_s *last_header)
{
  ssize_t buf_len = srv->send_buffer.w - srv->send_buffer.r;
  ssize_t res = 0, msg_len = 0;
  size_t i;

  for (i=0; i<iov_cnt; i++)
    msg_len += iov[i].iov_len;
  if ((size_t) (buf_len + msg_len) > mc->send_buffer_max)
    return OMCACHE_BUFFER_FULL;

  // set last_req_sent field now that we're about to send (or buffer) this
  srv->last_req_sent = last_header->opaque;
  if (!omc_is_request_quiet(last_header->opcode))
    srv->last_req_sent_nq = srv->last_req_sent;
  omc_srv_debug(srv, "%c sending %zu messages, last: type 0x%hhx, id %u %s",
                srv->connected ? '+' : '-', req_cnt,
                last_header->opcode, last_header->opaque,
                omc_is_request_quiet(last_header->opcode) ? "(quiet)" : "");

  // make sure we're meant to write immediately and the connection is
  // established and the existing write buffer empty
  if (srv->connected && mc->buffer_writes == false && buf_len == 0)
    {
      res = writev(srv->sock, iov, iov_cnt);
      srv->last_io_attempt = omc_msec();
      if (res > 0)
        {
          srv->last_io_success = srv->last_io_attempt;
          omc_srv_debug(srv, "writev sent %s requests",
                        res == msg_len ? "all" : "some");
        }
      if (res == -1 && errno != EINTR && errno != EAGAIN)
        {
          omc_srv_reset(mc, srv, "writev failed");
        }
      else
        {
          omc_srv_debug(srv, "writev %zd bytes of %zd bytes %s",
                        res, msg_len, (res == -1) ? strerror(errno) : "");
        }
    }
  if (res == msg_len)
    return OMCACHE_OK;

  // buffer everything we didn't write
  if (srv->send_buffer.end - srv->send_buffer.w < msg_len)
    {
      if (buf_len > 0 && srv->send_buffer.r != srv->send_buffer.base)
        {
          // move buffered data to the base to make room for the new content
          memmove(srv->send_buffer.base, srv->send_buffer.r, buf_len);
        }
      if (srv->send_buffer.end - srv->send_buffer.base < buf_len + msg_len)
        {
          size_t buf_req = min((buf_len + msg_len) * 3 / 2, mc->send_buffer_max);
          // reallocate a larger buffer
          srv->send_buffer.base = realloc(srv->send_buffer.base, buf_req);
          srv->send_buffer.end = srv->send_buffer.base + buf_req;
          omc_srv_debug(srv, "reallocated send buffer, now %zu bytes", buf_req);
        }
      srv->send_buffer.r = srv->send_buffer.base;
      srv->send_buffer.w = srv->send_buffer.base + buf_len;
    }

  for (i=0; i<iov_cnt; i++)
    {
      if ((size_t) res >= iov[i].iov_len)
        {
          res -= iov[i].iov_len;
          continue;
        }
      size_t part_len = iov[i].iov_len - res;
      memmove(srv->send_buffer.w, ((const unsigned char *) iov[i].iov_base) + res, part_len);
      srv->send_buffer.w += part_len;
      res = 0;
    }

  return OMCACHE_BUFFERED;
}

static void omc_req_id_check(omcache_t *mc, size_t req_count)
{
  // Note that we must take into account the fact that we may send implicit
  // NOOPs to disconnected servers at this point so don't push the limit
  if (UINT32_MAX - mc->req_id > (mc->server_count + req_count) * 2)
    return;
  omc_log(LOG_INFO, "performing req_id wraparound %u -> %u to handle %zu requests",
          mc->req_id, 42, req_count);
  mc->req_id = 42;
  for (int i = 0; i < mc->server_count; i++)
    if (mc->servers[i]->connected)
      {
        mc->servers[i]->last_req_recvd = 0;
        omc_srv_send_noop(mc, mc->servers[i], false);
      }
}

static int omc_srv_send_noop(omcache_t *mc, omc_srv_t *srv, bool init)
{
  omc_req_id_check(mc, 1);
  struct omcache_req_header_s hdr = {
    .magic = PROTOCOL_BINARY_REQ,
    .opcode = PROTOCOL_BINARY_CMD_NOOP,
    .datatype = PROTOCOL_BINARY_RAW_BYTES,
    .opaque = ++ mc->req_id,
    };
  if (init)
    srv->expected_noop = mc->req_id;
  struct iovec iov[] = {{ .iov_len = sizeof(hdr), .iov_base = &hdr }};
  return omc_srv_submit(mc, srv, iov, 1, 1, &hdr);
}

omcache_server_info_t *omcache_server_info(omcache_t *mc, int server_index)
{
  if (server_index >= mc->server_count || server_index < 0)
    return NULL;
  omc_srv_t *srv = mc->servers[server_index];
  omcache_server_info_t *info = calloc(1, sizeof(*info));
  info->omcache_version = OMCACHE_VERSION;
  info->server_index = server_index;
  info->hostname = strdup(srv->hostname);
  info->port = atoi(srv->port);
  return info;
}

int omcache_server_info_free(omcache_t *mc __attribute__((unused)), omcache_server_info_t *info)
{
  free(info->hostname);
  free(info);
  return OMCACHE_OK;
}

int omcache_command_status(omcache_t *mc, omcache_req_t *req, int32_t timeout_msec)
{
  size_t req_count = 1, value_count = 1;
  omcache_value_t value = {0};
  int req_srv_idx = req->server_index;

  int ret = omcache_command(mc, req, &req_count, &value, &value_count, timeout_msec);
  if (value_count)
    return value.status;
  if (ret != OMCACHE_OK || timeout_msec == 0)
    return ret;
  // is the server dead or did our response just disappear in thin air?
  // note that in case the caller asked for a specific server we'll say that
  // servers are not available, if we picked the server from our pool we'll
  // say communication with one server failed
  if (!mc->servers[req->server_index]->connected)
    return req_srv_idx >= 0 ? OMCACHE_NO_SERVERS : OMCACHE_SERVER_FAILURE;
  return OMCACHE_FAIL;
}

int omcache_command(omcache_t *mc,
                    omcache_req_t *reqs, size_t *req_countp,
                    omcache_value_t *values, size_t *value_count,
                    int32_t timeout_msec)
{
  int ret = OMCACHE_OK;
  int buffered = 0;
  size_t req_count = *req_countp;
  *req_countp = 0;

  if (mc->server_count == 0)
    ret = OMCACHE_NO_SERVERS;

  if (ret != OMCACHE_OK || req_count == 0)
    {
      if (value_count)
        *value_count = 0;
      return ret;
    }

  struct rpsbucket_s
  {
    omcache_req_t *reqs;
    size_t count;
    size_t size;
  } reqs_per_server[mc->server_count];
  memset(reqs_per_server, 0, sizeof(reqs_per_server));

  if (mc->server_count == 1)
    {
      reqs_per_server[0].reqs = reqs;
      reqs_per_server[0].count = req_count;
    }
  else
    {
      for (size_t i=0; i<req_count; i++)
        {
          omcache_req_t *req = &reqs[i];
          size_t h_keylen = be16toh(req->header.keylen);
          int server_index = (req->server_index != -1)
              ? (req->server_index)
              : omc_ketama_lookup(mc, req->key, h_keylen);

          if (server_index >= mc->server_count || server_index < 0)
            {
              if (req->server_index != -1)
                omc_log(LOG_NOTICE, "dropping request, server index %d is not valid", server_index);
              ret = OMCACHE_NO_SERVERS;
              continue;
            }

          struct rpsbucket_s *rps = &reqs_per_server[server_index];
          // try to flush out anything pending for the server if this is the
          // first time we touch it
          if (rps->count == 0 && mc->buffer_writes == false)
            {
              omc_srv_t *srv = mc->servers[server_index];
              ret = omc_srv_io(mc, srv, NULL);
              omc_srv_debug(srv, "io: %s", omcache_strerror(ret));
              if (ret != OMCACHE_AGAIN && ret != OMCACHE_OK)
                {
                  omc_srv_log(LOG_NOTICE, srv, "dropping request, flush failed: %s", omcache_strerror(ret));
                  ret = OMCACHE_NO_SERVERS;
                  // if we used ketama we'll reschedule the request if the
                  // originally selected server was offlined by omc_srv_io
                  if (req->server_index == -1 && srv->disabled)
                    i --;
                  continue;
                }
              ret = OMCACHE_OK;
            }
          if (req_count == 1)
            {
              // minor optimization when calling this api with a single request
              rps->reqs = reqs;
              rps->count = 1;
              break;
            }
          if (rps->size <= rps->count)
            {
              rps->size += 16;
              rps->reqs = realloc(rps->reqs, rps->size * sizeof(omcache_req_t));
            }
          rps->reqs[rps->count ++] = *req;
        }
    }

  // Force wraparound if we don't have enough req_ids available before it
  omc_req_id_check(mc, req_count);

  for (int i = 0; i < mc->server_count; i ++)
    {
      struct rpsbucket_s *rps = &reqs_per_server[i];
      if (rps->count == 0)
        continue;

      omc_srv_t *srv = mc->servers[i];
      struct iovec iov[min(4 * rps->count, g_iov_max)];
      int iov_idx = 0, iov_req_count = 0;
      size_t srv_reqs_sent = 0;

      for (size_t ri = 0; ri < rps->count; ri ++)
        {
          omcache_req_t *req = &rps->reqs[ri];
          size_t h_keylen = be16toh(req->header.keylen);
          size_t h_datalen = be32toh(req->header.bodylen) - h_keylen - req->header.extlen;
          req->server_index = srv->list_index;
          // set the common magic numbers for request
          req->header.magic = PROTOCOL_BINARY_REQ;
          req->header.datatype = PROTOCOL_BINARY_RAW_BYTES;
          // set an incrementing request id to each request
          req->header.opaque = ++ mc->req_id;
          omc_srv_debug(srv, "%c queuing command: type 0x%hhx, id %u %s",
                        srv->connected ? '+' : '-',
                        req->header.opcode, req->header.opaque,
                        omc_is_request_quiet(req->header.opcode) ? "(quiet)" : "");
          // construct the iovector
          iov[iov_idx ++] = (struct iovec) { .iov_base = &req->header, .iov_len = sizeof(req->header) };
          if (req->header.extlen)
            iov[iov_idx ++] = (struct iovec) { .iov_base = (void *) req->extra, .iov_len = req->header.extlen };
          if (h_keylen)
            iov[iov_idx ++] = (struct iovec) { .iov_base = (void *) req->key, .iov_len = h_keylen };
          if (h_datalen)
            iov[iov_idx ++] = (struct iovec) { .iov_base = (void *) req->data, .iov_len = h_datalen };
          iov_req_count ++;

          // submit tasks if we're running out of iovec space or are done here
          if ((g_iov_max - iov_idx < 4) || (ri == rps->count - 1))
            {
              ret = omc_srv_submit(mc, srv, iov, iov_idx, iov_req_count, &req->header);
              if (ret != OMCACHE_OK && ret != OMCACHE_BUFFERED)
                {
                  omc_srv_log(LOG_WARNING, srv, "submitting %d requests failed, not sending %zu more",
                              iov_req_count, rps->count - ri - 1);
                  break;
                }
              else if (ret == OMCACHE_BUFFERED)
                buffered ++;
              srv_reqs_sent = ri + 1;
              iov_idx = 0;
              iov_req_count = 0;
            }
        }
      // copy sent requests back to the original 'reqs' array so we can look them up later
      if (srv_reqs_sent)
        {
          // NOTE: in case of 1 server or 1 request reqs and rps->reqs may point to the same place
          memmove(reqs + *req_countp, rps->reqs, srv_reqs_sent * sizeof(omcache_req_t));
          *req_countp += srv_reqs_sent;
        }
      if (rps->size)
        free(rps->reqs);
    }

  if (timeout_msec == 0 || *req_countp == 0)
    {
      // no response requested or data wasn't sent. we're done.
      if (value_count)
        *value_count = 0;
      if (ret == OMCACHE_OK && buffered > 0)
        ret = OMCACHE_BUFFERED;
      return ret;
    }

  // look for responses to the queries we just sent
  return omcache_io(mc, reqs, req_countp, values, value_count, timeout_msec);
}
