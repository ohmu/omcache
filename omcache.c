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

#include "memcached_protocol_binary.h"
#include "omcache.h"

#define MC_PORT "11211"

#define max(a,b) ({__typeof__(a) a_ = (a), b_ = (b); a_ > b_ ? a_ : b_; })
#define min(a,b) ({__typeof__(a) a_ = (a), b_ = (b); a_ < b_ ? a_ : b_; })


#define omc_log(pri,fmt,...) ({ \
    if (mc->log_cb) { \
      char *log_msg_; \
      asprintf(&log_msg_, "[%.03f] omcache/%s:%d: " fmt, \
               (omc_msec() - mc->init_msec) / 1000.0, __func__, __LINE__, __VA_ARGS__); \
      mc->log_cb(mc->log_context, (pri), log_msg_); \
      free(log_msg_); \
    } NULL; })
#define omc_srv_log(pri,srv,fmt,...) \
    omc_log(pri, "[%s:%s] " fmt, (srv)->hostname, (srv)->port, __VA_ARGS__)

#ifdef DEBUG
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
  uint32_t last_nq_req_sent;
  bool last_req_quiet;
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
  int *fd_map;
  int fd_map_max;

  // distribution
  omc_ketama_t *ketama;

  // settings
  omcache_log_callback_func *log_cb;
  void *log_context;

  omcache_response_callback_func *resp_cb;
  void *resp_cb_context;

  size_t recv_buffer_max;
  size_t send_buffer_max;
  uint32_t connect_timeout_msec;
  uint32_t reconnect_timeout_msec;
  uint32_t dead_timeout_msec;
  bool buffer_writes;
};

static int omc_srv_free(omcache_t *mc, omc_srv_t *srv);
static int omc_srv_connect(omcache_t *mc, omc_srv_t *srv);
static int omc_srv_send_noop(omcache_t *mc, omc_srv_t *srv, bool init);
static omc_ketama_t *omc_ketama_create(omcache_t *mc);
static inline int64_t omc_msec();


omcache_t *omcache_init(void)
{
  omcache_t *mc = calloc(1, sizeof(*mc));
  mc->init_msec = omc_msec();
  mc->req_id = time(NULL);
  mc->recv_buffer_max = 1024 * (1024 + 32);
  mc->send_buffer_max = 1024 * (1024 * 10);
  mc->connect_timeout_msec = 10 * 1000;
  mc->reconnect_timeout_msec = 10 * 1000;
  mc->dead_timeout_msec = 10 * 1000;
  mc->fd_map_max = 1024;
  mc->fd_map = malloc(mc->fd_map_max * sizeof(*mc->fd_map));
  memset(mc->fd_map, 0xff, mc->fd_map_max * sizeof(*mc->fd_map));
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
  free(mc->ketama);
  free(mc->fd_map);
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
    case OMCACHE_DELTA_BAD_VALUE: return "Existing value can not be incremented or decremented";
    case OMCACHE_FAIL: return "Command failed in memcached";
    case OMCACHE_AGAIN: return "Call would block, try again";
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
    default:
      return OMCACHE_FAIL;
    }
}

void omcache_log_stderr(void *context __attribute__((unused)),
                        int level __attribute__((unused)),
                        const char *msg)
{
  fprintf(stderr, "%s\n", msg);
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

static omc_srv_t *omc_srv_init(const char *hostname, int port)
{
  omc_srv_t *srv = calloc(1, sizeof(*srv));
  srv->sock = -1;
  srv->list_index = -1;
  if (port == -1)
    {
      const char *p = strchr(hostname, ':');
      if (p != NULL)
        {
          srv->hostname = strndup(hostname, p - hostname);
          srv->port = strdup(p + 1);
        }
      else
        {
          srv->hostname = strdup(hostname);
          srv->port = strdup(MC_PORT);
        }
    }
  else
    {
      srv->hostname = strdup(hostname);
      asprintf(&srv->port, "%d", port);
    }
  return srv;
}

static int omc_srv_free(omcache_t *mc, omc_srv_t *srv)
{
  if (srv->sock >= 0)
    {
      shutdown(srv->sock, SHUT_RDWR);
      close(srv->sock);
      mc->fd_map[srv->sock] = -1;
      srv->sock = -1;
    }
  free(srv->send_buffer.base);
  free(srv->recv_buffer.base);
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
      srv_new[srv_new_count++] = omc_srv_init(srv, -1);
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
  // reset list indexes
  for (ssize_t i=0; i<mc->server_count; i++)
    {
      omc_srv_debug(mc->servers[i], "server #%zd", i);
      mc->servers[i]->list_index = i;
    }

  // rerun distribution
  free(mc->ketama);
  mc->ketama = omc_ketama_create(mc);
  return OMCACHE_OK;
}

int omcache_set_log_callback(omcache_t *mc, omcache_log_callback_func *func, void *context)
{
  mc->log_cb = func;
  mc->log_context = context;
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
      if (srv->last_req_quiet == true)
        {
          omc_srv_send_noop(mc, srv, false);
        }
      if (srv->last_req_recvd < srv->last_nq_req_sent)
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
          if (srv->conn_timeout > 0)
            *poll_timeout = min(*poll_timeout, max(1, now - srv->conn_timeout));
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

static uint32_t omc_hash_jenkins_oat(const unsigned char *key, size_t key_len)
{
  // http://en.wikipedia.org/wiki/Jenkins_hash_function#one-at-a-time
  uint32_t hash, i;
  for (hash=i=0; i<key_len; i++)
    {
      hash += key[i];
      hash += (hash << 10);
      hash ^= (hash >> 6);
    }
  hash += (hash << 3);
  hash ^= (hash >> 11);
  hash += (hash << 15);
  return hash;
}

static uint32_t omc_ketama_hash(const omc_srv_t *srv, uint32_t point)
{
  size_t hostname_len = strlen(srv->hostname), port_len = strlen(srv->port);
  unsigned char name[hostname_len + port_len + 16], *namep;
  namep = mempcpy(name, srv->hostname, hostname_len);
  // libmemcached ketama appends port number to hostname if it's not 11211
  if (strcmp(srv->port, MC_PORT) != 0)
    {
      *namep++ = ':';
      namep = mempcpy(namep, srv->port, port_len);
    }
  namep += snprintf((char*) namep, 14, "-%u", point);
  // libmemcached ketama uses Bob Jenkins' "one at a time" hashing
  return omc_hash_jenkins_oat(name, namep - name);
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
  // libmemcached ketama has 100 points per server, we use the same to be compatible
  size_t points_per_entry = 100;
  size_t cidx = 0, total_points = mc->server_count * points_per_entry;
  omc_ketama_t *ktm = (omc_ketama_t *) malloc(sizeof(omc_ketama_t) + total_points * sizeof(omc_ketama_point_t));

  ktm->point_count = total_points;
  for (ssize_t i=0; i<mc->server_count; i++)
    {
      for (size_t p=0; p<points_per_entry; p++)
        {
          ktm->points[cidx++] = (omc_ketama_point_t) {
            .srv = mc->servers[i],
            .hash_value = omc_ketama_hash(mc->servers[i], p),
            };
        }
    }

  qsort(ktm->points, total_points, sizeof(omc_ketama_point_t), omc_ketama_point_cmp);

  return ktm;
}

static omc_srv_t *omc_ketama_lookup(omcache_t *mc, const unsigned char *key, size_t key_len)
{
  uint32_t hash_value = omc_hash_jenkins_oat(key, key_len);
  const omc_ketama_point_t *first = mc->ketama->points, *last = mc->ketama->points + mc->ketama->point_count;
  const omc_ketama_point_t *left = first, *right = last, *selected;
  bool wrap = false;

  while (left < right)
    {
      const omc_ketama_point_t *middle = left + (right - left) / 2;
      if (middle->hash_value < hash_value)
        left = middle + 1;
      else
        right = middle;
    }

  // skip disabled servers
  for (selected = (right == last) ? first : right;
      selected == last || selected->srv->disabled;
      selected++)
    {
      if (selected != last)
        {
          omc_srv_log(LOG_NOTICE, selected->srv, "%s", "ketama skipping disabled server");
          continue;
        }
      if (wrap)
        {
          omc_log(LOG_ERR, "%s", "all servers are disabled");
          return NULL;
        }
      wrap = true;
      selected = first;
    }
  return selected->srv;
}

int omcache_server_index_for_key(omcache_t *mc, const unsigned char *key, size_t key_len)
{
  if (mc->server_count > 1)
    return omc_ketama_lookup(mc, key, key_len)->list_index;
  return 0;
}

static void
omc_srv_reset(omcache_t *mc, omc_srv_t *srv, const char *log_msg)
{
  omc_srv_log(LOG_NOTICE, srv, "reset: %s (%s)", log_msg, strerror(errno));
  if (srv->sock != -1)
    {
      close(srv->sock);
      mc->fd_map[srv->sock] = -1;
    }
  srv->connected = false;
  srv->sock = -1;
  srv->conn_timeout = 0;
  srv->last_req_recvd = 0;
  srv->last_req_sent = 0;
  srv->last_req_quiet = false;
  srv->recv_buffer.r = srv->recv_buffer.base;
  srv->recv_buffer.w = srv->recv_buffer.base;
  srv->send_buffer.r = srv->send_buffer.base;
  srv->send_buffer.w = srv->send_buffer.base;
  if (srv->expected_noop)
    {
      srv->retry_at = omc_msec() + mc->reconnect_timeout_msec;
      srv->disabled = true;
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
      // refreshed them > 10 seconds ago
      if (srv->addrs == NULL || now - srv->last_gai > 10000)
        {
          if (srv->addrs)
            {
              freeaddrinfo(srv->addrs);
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
              srv->addrs = NULL;
              return OMCACHE_SERVER_FAILURE;
            }
          srv->addrp = srv->addrs;
          srv->last_gai = now;
        }
      while (srv->addrp)
        {
          int sock = socket(srv->addrp->ai_family, srv->addrp->ai_socktype, srv->addrp->ai_protocol);
          fcntl(sock, F_SETFL, O_NONBLOCK);
          if (sock >= mc->fd_map_max)
            {
              int old_max = mc->fd_map_max;
              mc->fd_map_max += 16;
              mc->fd_map = realloc(mc->fd_map, mc->fd_map_max * sizeof(*mc->fd_map));
              memset(&mc->fd_map[old_max], 0xff, 16 * sizeof(*mc->fd_map));
            }
          mc->fd_map[sock] = srv->list_index;
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
      freeaddrinfo(srv->addrs);
      srv->addrs = NULL;
      if (srv->sock == -1)
        {
          omc_srv_reset(mc, srv, "no connection established");
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
          err = SO_ERROR;
        }
      if (err)
        {
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
      size_t new_size = min(buf_max, buffered + required + 8192);
      buf->base = realloc(buf->base, new_size);
      buf->end = buf->base + new_size;
    }

  buf->r = buf->base;
  buf->w = buf->base + buffered;
  return OMCACHE_OK;
}

static int omc_do_read(omcache_t *mc, omc_srv_t *srv, size_t msg_size)
{
  // make sure we have room for at least the requested bytes, but read as much as possible
  size_t space = srv->recv_buffer.end - srv->recv_buffer.w;
  if (space < msg_size)
    {
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
  if (res > 0)
    {
      srv->recv_buffer.w += res;
      srv->last_io_success = srv->last_io_attempt;
    }
  return OMCACHE_OK;
}

// read any responses returned by the server calling mc->resp_cb on them.
// if a response's 'opaque' matches req_id store that response in *resp.
static int omc_srv_read(omcache_t *mc, omc_srv_t *srv,
                        uint32_t req_id, omcache_resp_t *resp)
{
  bool keep_buffers = false;
  int res = omc_do_read(mc, srv, 8192);
  size_t space = srv->recv_buffer.end - srv->recv_buffer.w;

  // handle as many messages as possible
  for (int i=0; srv->recv_buffer.r != srv->recv_buffer.w; i++)
    {
      omcache_resp_t rresp = {NULL, NULL, NULL};
      protocol_binary_response_header *hdr =
        (protocol_binary_response_header *) srv->recv_buffer.r;
      size_t buffered = srv->recv_buffer.w - srv->recv_buffer.r;
      size_t msg_size = sizeof(protocol_binary_response_header);
      if (buffered < msg_size)
        {
          omc_srv_debug(srv, "msg %d: not enough data in buffer (%zd, need %zd)",
                        i, buffered, msg_size);
          break;
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
          if (i > 0 || space > 0 || keep_buffers)
            {
              // we already processed some messages or server didn't send
              // the entire response to use although we had space in the
              // receive buffer, get back to this next time
              break;
            }
          // our receive buffer isn't big enough for this, reallocate it and read again
          res = omc_do_read(mc, srv, msg_size);
          if (res == OMCACHE_BUFFER_FULL)
            {
              // the message is too big to fit in our receive buffer at all,
              // discard it (by resetting connection.)
              if (mc->resp_cb)
                {
                  rresp.header = hdr;
                  rresp.key = srv->recv_buffer.r + sizeof(hdr->bytes) + hdr->response.extlen;
                  mc->resp_cb(mc, OMCACHE_BUFFER_FULL, &rresp, mc->resp_cb_context);
                }
              errno = EMSGSIZE;
              omc_srv_reset(mc, srv, "buffer full - can't handle response");
            }
          if (res != OMCACHE_OK)
            break;
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
              srv->disabled = false;
              srv->recv_buffer.r += msg_size;
              omc_srv_debug(srv, "%s", "received expected noop packet");
              continue;
            }
        }

      // setup response object
      rresp.header = hdr;
      rresp.key = srv->recv_buffer.r + sizeof(hdr->bytes) + hdr->response.extlen;
      rresp.data = rresp.key + be16toh(hdr->response.keylen);

      srv->recv_buffer.r += msg_size;

      // pass it to response callback
      if (mc->resp_cb)
        {
          mc->resp_cb(mc, omc_map_mc_status_to_ret_code(be16toh(hdr->response.status)),
                      &rresp, mc->resp_cb_context);
        }

      // return if it it was requested
      if (hdr->response.opaque == req_id && resp != NULL)
        {
          omc_srv_debug(srv, "got expected packet %u, not reading anymore", req_id);
          resp->header = rresp.header;
          resp->key = rresp.key;
          resp->data = rresp.data;
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
  if (srv->last_req_recvd >= srv->last_nq_req_sent)
    return OMCACHE_OK;
  return OMCACHE_AGAIN;
}

// try to write/connect if there's pending data to this server.  read any
// responses returned by the server calling mc->resp_cb on them.  if a
// response's 'opaque' matches req_id store that response in *resp.
static int omc_srv_io(omcache_t *mc, omc_srv_t *srv,
                      uint32_t req_id, omcache_resp_t *resp)
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
  if (srv->conn_timeout == 0 && srv->sock >= 0 && srv->last_req_recvd < srv->last_nq_req_sent)
    {
      ret = omc_srv_read(mc, srv, req_id, resp);
      if (ret == OMCACHE_AGAIN)
        again_r = true;
      else if (ret != OMCACHE_OK)
        return ret;
    }
  return (again_w || again_r) ? OMCACHE_AGAIN : OMCACHE_OK;
}

// process writes and reads until we see a response to req_id or until timeout_msec has passed
// if req_id is given the relevant response will be stored in *resp
int omcache_io(omcache_t *mc, int32_t timeout_msec, uint32_t req_id, omcache_resp_t *resp)
{
  int again, ret = OMCACHE_OK;
  int64_t now = omc_msec();
  int64_t timeout_abs = (timeout_msec > 0) ? now + timeout_msec : - 1;

  if (resp)
    {
      resp->header = NULL;
      resp->key = NULL;
      resp->data = NULL;
    }

  while (ret == OMCACHE_OK || ret == OMCACHE_AGAIN)
    {
      omc_debug("looking for %u timeout in %lld msec",
                req_id, (long long) (timeout_abs != -1 ? timeout_abs - omc_msec() : -1));
      if (timeout_abs >= 0)
        {
          now = omc_msec();
          if (now > timeout_abs)
            {
              omc_debug("%s", "omcache_io timeout");
              return OMCACHE_AGAIN;
            }
          timeout_msec = timeout_abs - now;
        }

      again = 0;
      int nfds = -1, timeout_poll = -1, polls __attribute__((unused)) = -1;
      struct pollfd *pfds = omcache_poll_fds(mc, &nfds, &timeout_poll);
      if (nfds == 0)
        {
          ret = OMCACHE_OK;
          break;
        }
      timeout_poll = (timeout_msec > 0) ? min(timeout_msec, timeout_poll) : timeout_poll;
      polls = poll(pfds, nfds, timeout_poll);
      omc_debug("poll(%d, %d): %d %s", nfds, timeout_poll, polls, polls == -1 ? strerror(errno) : "");
      now = omc_msec();
      for (int i=0; i<nfds; i++)
        {
          omc_srv_t *srv = mc->servers[mc->fd_map[pfds[i].fd]];
          if (!pfds[i].revents)
            {
              // reset connections that have failed
              if (now - srv->last_io_attempt > mc->dead_timeout_msec)
                {
                  errno = ETIMEDOUT;
                  omc_srv_reset(mc, srv, "io timeout");
                  again ++;
                }
              continue;
            }
          ret = omc_srv_io(mc, srv, req_id, resp);
          omc_srv_debug(srv, "io: %s", omcache_strerror(ret));
          if (ret == OMCACHE_AGAIN)
            again ++;
          else if (ret != OMCACHE_OK)
            break;
        }

      // break the or we found req_id: the receive buffer could be
      // overwritten by new calls to omc_srv_io so we need to allow the
      // caller to process the response before resuming reads.
      if (resp && resp->header)
        {
          // don't want to return OMCACHE_AGAIN if we found our key
          ret = OMCACHE_OK;
          again = 0;
          break;
        }

      // break the loop in case we didn't want to poll
      if (timeout_msec == 0)
        break;
    }
  if (ret == OMCACHE_OK && resp && resp->header == NULL)
    ret = OMCACHE_AGAIN;
  if (ret == OMCACHE_OK && again > 0)
    ret = OMCACHE_AGAIN;
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
      srv->last_nq_req_sent = srv->last_req_sent;
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
                          struct iovec *iov, size_t iov_cnt)
{
  ssize_t buf_len = srv->send_buffer.w - srv->send_buffer.r;
  ssize_t res = 0, msg_len = 0;
  size_t i;

  for (i=0; i<iov_cnt; i++)
    msg_len += iov[i].iov_len;
  if ((size_t) (buf_len + msg_len) > mc->send_buffer_max)
    return OMCACHE_BUFFER_FULL;

  // set last_req_sent field now that we're about to send (or buffer) this
  protocol_binary_request_header *hdr = iov[0].iov_base;
  srv->last_req_sent = hdr->request.opaque;
  srv->last_req_quiet = omc_is_request_quiet(hdr->request.opcode);
  if (!srv->last_req_quiet)
    srv->last_nq_req_sent = srv->last_req_sent;
  omc_srv_debug(srv, "%c sending message: type 0x%hhx, id %u %s",
                srv->connected ? '+' : '-',
                hdr->request.opcode, hdr->request.opaque,
                srv->last_req_quiet ? "(quiet)" : "");

  // make sure we're meant to write immediately and the connection is
  // established and the existing write buffer empty
  if (srv->connected && mc->buffer_writes == false && buf_len == 0)
    {
      res = writev(srv->sock, iov, iov_cnt);
      srv->last_io_attempt = omc_msec();
      if (res > 0)
        {
          srv->last_io_success = srv->last_io_attempt;
          omc_srv_debug(srv, "writev sent type 0x%hhx, id %u",
                        hdr->request.opcode, hdr->request.opaque);
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

static int omc_srv_send_noop(omcache_t *mc, omc_srv_t *srv, bool init)
{
  protocol_binary_request_noop req;
  memset(&req, 0, sizeof(req));

  req.message.header.request.magic = PROTOCOL_BINARY_REQ;
  req.message.header.request.opcode = PROTOCOL_BINARY_CMD_NOOP;
  req.message.header.request.datatype = PROTOCOL_BINARY_RAW_BYTES;
  req.message.header.request.opaque = ++ mc->req_id;
  if (init)
    srv->expected_noop = mc->req_id;
  struct iovec iov[] = {{ .iov_len = sizeof(req.bytes), .iov_base = req.bytes }};
  return omc_srv_submit(mc, srv, iov, 1);
}

int omcache_command(omcache_t *mc, omcache_req_t *req, omcache_resp_t *resp,
                    int server_index, int32_t timeout_msec)
{
  int ret = OMCACHE_NO_SERVERS;
  protocol_binary_request_header *hdr = (protocol_binary_request_header *) req->header;
  size_t h_keylen = be16toh(hdr->request.keylen);
  size_t h_datalen = be32toh(hdr->request.bodylen) - h_keylen - hdr->request.extlen;

  omc_srv_t *srv = NULL;
  if (server_index == -1)
    {
      if (mc->server_count == 1)
        srv = mc->servers[0];
      else if (mc->server_count > 1)
        srv = omc_ketama_lookup(mc, req->key, h_keylen);
    }
  else if (server_index >= 0 && server_index < mc->server_count)
    {
      srv = mc->servers[server_index];
    }

  if (srv)
    {
      // try to flush out anything pending for the server
      if (mc->buffer_writes == false)
        {
          ret = omc_srv_io(mc, srv, 0, NULL);
          omc_srv_debug(srv, "io: %s", omcache_strerror(ret));
        }

      // set the common magic numbers for request
      hdr->request.magic = PROTOCOL_BINARY_REQ;
      hdr->request.datatype = PROTOCOL_BINARY_RAW_BYTES;
      // set an incrementing request id to each request
      hdr->request.opaque = ++ mc->req_id;

      // construct the iovector
      struct iovec iov[] = {
        { .iov_base = req->header, .iov_len = sizeof(hdr->bytes) + hdr->request.extlen },
        { .iov_base = (void *) req->key, .iov_len = h_keylen, },
        { .iov_base = (void *) req->data, .iov_len = h_datalen, },
        };
      ret = omc_srv_submit(mc, srv, iov, 3);
    }

  if (timeout_msec == 0 || (ret != OMCACHE_OK && ret != OMCACHE_BUFFERED))
    {
      // no response requested or data wasn't sent. we're done.
      if (resp)
        {
          memset(resp, 0, sizeof(*resp));
          // return OMCACHE_AGAIN if we were able to send the request but
          // didn't read a response because we got 0 timeout
          if (ret == OMCACHE_OK)
            ret = OMCACHE_AGAIN;
        }
      return ret;
    }

  // we need a response to figure out status even if the caller doesn't want
  // a response object except when we're dealing with a stat command that
  // is expected to receive multiple response packets.
  omcache_resp_t osresp;
  if (resp == NULL && hdr->request.opcode != PROTOCOL_BINARY_CMD_STAT)
    resp = &osresp;

  // look for the response to the query we just sent
  ret = omcache_io(mc, timeout_msec, hdr->request.opaque, resp);
  if (ret == OMCACHE_OK && resp && resp->header)
    {
      // map response code to the status returned by server
      protocol_binary_response_header *rhdr =
        (protocol_binary_response_header *) resp->header;
      ret = omc_map_mc_status_to_ret_code(be16toh(rhdr->response.status));
    }
  return ret;
}

omcache_server_info_t *omcache_server_info(omcache_t *mc, int server_index)
{
  if (server_index >= mc->server_count || server_index < 0)
    return NULL;
  omc_srv_t *srv = mc->servers[server_index];
  omcache_server_info_t *info = calloc(1, sizeof(*info));
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
