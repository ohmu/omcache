/*
 * OMcache - a memcached client library
 *
 * Copyright (c) 2013, Oskari Saarenmaa <os@ohmu.fi>
 * All rights reserved.
 *
 * This file is under the 2-clause BSD license.
 * See the file `LICENSE` for details.
 *
 */

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
#include <sys/types.h>
#include <sys/uio.h>

#include <memcached/protocol_binary.h>

#define MC_PORT "11211"

#define max(a,b) ({__typeof__(a) a_ = (a), b_ = (b); a_ > b_ ? a_ : b_; })
#define min(a,b) ({__typeof__(a) a_ = (a), b_ = (b); a_ < b_ ? a_ : b_; })

#include "omcache.h"
#include "oconst.h"

#define omcache_log(fmt,...) ({ if (mc->log_func) mc->log_func(mc, 0, "omcache: " fmt "\n", __VA_ARGS__); NULL; })
#define omcache_srv_log(srv,fmt,...) omcache_log("[%s:%s] " fmt, (srv)->hostname, (srv)->port, __VA_ARGS__)

#define OMCACHE_SEND_BUFFER_SIZE 1024*1024

typedef struct omcache_buffer_s
{
  unsigned char *base;
  unsigned char *end;
  unsigned char *r;
  unsigned char *w;
} omcache_buffer_t;

typedef struct omcache_clock_s
{
  clockid_t type;
  struct timespec ts;
} omcache_clock_t;

struct omcache_server_s
{
  int list_index;
  int sock;
  char *hostname;
  char *port;
  struct addrinfo *addrs, *addrp;
  int64_t last_gai;
  int64_t conn_timeout;
  unsigned int last_req_recvd;
  unsigned int last_req_sent;
  unsigned int last_req_quiet: 1;
  omcache_buffer_t send_buffer;
  omcache_buffer_t recv_buffer;
};

struct omcache_s
{
  unsigned int req_id;
  omcache_clock_t now;
  omcache_server_t **servers;
  struct pollfd *server_polls;
  ssize_t server_count;

  // settings
  omcache_log_func *log_func;
  void *log_context;
  omcache_dist_init_func *dist_init;
  omcache_dist_free_func *dist_free;
  omcache_dist_lookup_func *dist_lookup;
  void *dist_init_context;
  void *dist_context;

  size_t send_buffer_max;
  unsigned int conn_timeout_msec;
  unsigned int buffer_writes: 1;
  int replicate_writes;
};

static struct timespec *omcache_time(omcache_t *mc, bool monotonic);
static int omcache_server_free(omcache_server_t *srv);
static int omcache_srv_connect(omcache_t *mc, omcache_server_t *srv);
static int omcache_srv_send_noop(omcache_t *mc, omcache_server_t *srv);

static inline void omcache_enter(omcache_t *mc)
{
  mc->now.type = -1;
}

omcache_t *omcache_init(void)
{
  omcache_t *mc = calloc(1, sizeof(*mc));
  omcache_enter(mc);
  mc->req_id = omcache_time(mc, false)->tv_sec;
  mc->send_buffer_max = 1024 * 1024 * 10;
  mc->conn_timeout_msec = 10000;
  mc->dist_lookup = omcache_dist_modulo_lookup;
  mc->dist_context = mc;
  return mc;
}

int omcache_free(omcache_t *mc)
{
  omcache_enter(mc);
  if (mc->servers)
    {
      for (off_t i=0; i<mc->server_count; i++)
        {
          if (mc->servers[i])
            {
              omcache_server_free(mc->servers[i]);
              mc->servers[i] = NULL;
            }
        }
      memset(mc->servers, 'L', mc->server_count * sizeof(void *));
      free(mc->servers);
    }
  if (mc->dist_free != NULL)
    {
      mc->dist_free(mc->dist_context);
    }
  memset(mc, 'M', sizeof(*mc));
  free(mc);
  return OMCACHE_OK;
}

const char *omcache_strerror(omcache_t __attribute__((unused)) *mc, int rc)
{
  switch (rc)
    {
    case OMCACHE_OK: return "Success";
    case OMCACHE_FAIL: return "Failure";
    case OMCACHE_AGAIN: return "Again";
    case OMCACHE_BUFFERED: return "Buffered";
    case OMCACHE_BUFFER_FULL: return "Buffer full";
    case OMCACHE_NOT_FOUND: return "Not found";
    default: return "Unknown";
    }
}

void omcache_log_stderr(void *context __attribute__((unused)),
                        int level __attribute__((unused)),
                        const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
}

static struct timespec *omcache_time(omcache_t *mc, bool monotonic)
{
  clockid_t clk = monotonic ? CLOCK_MONOTONIC : CLOCK_REALTIME;
#ifdef CLOCK_MONOTONIC_COARSE
  clockid_t coarse = monotonic ? CLOCK_MONOTONIC_COARSE : CLOCK_REALTIME_COARSE;
#else
  clockid_t coarse = -1;
#endif
  if (mc->now.type != clk)
    {
      if (coarse == -1 || clock_gettime(coarse, &mc->now.ts) == -1)
        {
          clock_gettime(clk, &mc->now.ts);
        }
      mc->now.type = clk;
    }
  return &mc->now.ts;
}

static inline int64_t omcache_msec(omcache_t *mc)
{
  omcache_time(mc, true);
  return mc->now.ts.tv_sec * 1000 + mc->now.ts.tv_nsec / 1000000;
}

static omcache_server_t *omcache_server_init(const char *hostname, int port, int list_index)
{
  omcache_server_t *srv = calloc(1, sizeof(*srv));
  srv->sock = -1;
  srv->list_index = list_index;
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
      srv->port = malloc(12);
      snprintf(srv->port, 12, "%d", port);
    }
  return srv;
}

static int omcache_server_free(omcache_server_t *srv)
{
  if (srv->sock >= 0)
    {
      shutdown(srv->sock, SHUT_RDWR);
      close(srv->sock);
      srv->sock = -1;
    }
  if (srv->send_buffer.base)
    {
      free(srv->send_buffer.base);
    }
  if (srv->recv_buffer.base)
    {
      free(srv->recv_buffer.base);
    }
  memset(srv, 'S', sizeof(*srv));
  free(srv);
  return OMCACHE_OK;
}

int omcache_set_servers(omcache_t *mc, const char *servers)
{
  // XXX: support JSON server definitions, a list or an object with more properties
  char *srv_dup = strdup(servers), *srv = srv_dup;

  omcache_enter(mc);
  while (srv)
    {
      char *p = strchr(srv, ',');
      if (p)
        *p++ = 0;
      mc->servers = realloc(mc->servers, (mc->server_count + 1) * sizeof(void *));
      mc->servers[mc->server_count] = omcache_server_init(srv, -1, mc->server_count);
      mc->server_count += 1;
      srv = p;
    }
  free(srv_dup);
  mc->server_polls = realloc(mc->server_polls, mc->server_count * sizeof(*mc->server_polls));
  // rerun distribution
  if (mc->dist_free)
    {
      mc->dist_free(mc->dist_context);
      mc->dist_context = NULL;
    }
  if (mc->dist_init)
    {
      mc->dist_context = mc->dist_init(mc->servers, mc->server_count, mc->dist_init_context);
    }
  return OMCACHE_OK;
}

int omcache_set_log_func(omcache_t *mc, omcache_log_func *func, void *context)
{
  mc->log_func = func;
  mc->log_context = context;
  return OMCACHE_OK;
}

int omcache_set_dist_func(omcache_t *mc,
                          omcache_dist_init_func *init_func,
                          omcache_dist_free_func *free_func,
                          omcache_dist_lookup_func *lookup_func,
                          void *init_context)
{
  if (mc->dist_free)
    {
      mc->dist_free(mc->dist_context);
    }
  // use init function's return value as context if given
  mc->dist_init = init_func;
  mc->dist_free = free_func;
  mc->dist_lookup = lookup_func;
  mc->dist_init_context = init_context;
  mc->dist_context = init_context;
  if (init_func)
    mc->dist_context = init_func(mc->servers, mc->server_count, init_context);
  return OMCACHE_OK;
}

int omcache_set_conn_timeout(omcache_t *mc, unsigned int msec)
{
  mc->conn_timeout_msec = msec;
  return OMCACHE_OK;
}

int omcache_set_send_buffer_max_size(omcache_t *mc, size_t size)
{
  mc->send_buffer_max = size;
  return OMCACHE_OK;
}

struct pollfd *omcache_poll_fds(omcache_t *mc, int *nfds)
{
  int n, i;
  omcache_enter(mc);
  for (i=n=0; i<mc->server_count; i++)
    {
      omcache_server_t *srv = mc->servers[i];
      mc->server_polls[n].events = 0;
      if (srv->last_req_quiet == 1)
        {
          omcache_srv_send_noop(mc, srv);
          srv->last_req_quiet = 0;
        }
      if (srv->last_req_recvd < srv->last_req_sent && srv->last_req_quiet == 0)
        {
          mc->server_polls[n].events |= POLLIN;
        }
      if (srv->send_buffer.w != srv->send_buffer.r || srv->conn_timeout > 0)
        {
          mc->server_polls[n].events |= POLLOUT;
        }
      if (mc->server_polls[n].events != 0)
        {
          if (srv->sock < 0)
            {
              omcache_srv_connect(mc, srv);
            }
          mc->server_polls[n].fd = srv->sock;
          mc->server_polls[n].revents = 0;
          n ++;
        }
    }
  *nfds = n;
  return mc->server_polls;
}

static uint32_t omcache_hash_jenkins_oat(const unsigned char *key, size_t key_len)
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

static uint32_t omcache_ketama_hash(const void *entry, uint32_t point,
                                    void *context __attribute__((unused)))
{
  const omcache_server_t *srv = entry;
  size_t hostname_len = strlen(srv->hostname), port_len = strlen(srv->port);
  unsigned char name[hostname_len + port_len + 16], *namep;
  namep = mempcpy(name, srv->hostname, hostname_len);
  if (strcmp(srv->port, MC_PORT) != 0)
    {
      *namep++ = ':';
      namep = mempcpy(namep, srv->port, port_len);
    }
  namep += snprintf((char*) namep, 14, "-%u", point);
  return omcache_hash_jenkins_oat(name, namep-name);
}

void *omcache_dist_ketama_init(omcache_server_t **servers, size_t server_count, void *context __attribute__((unused)))
{
  // libmemcached ketama has 100 points per server, we use the same to be compatible
  return oconst_create((void **) servers, server_count, 100, omcache_ketama_hash, NULL);
}

omcache_server_t *omcache_dist_ketama_lookup(const unsigned char *key, size_t key_len, void *ketama)
{
  uint32_t hash = omcache_hash_jenkins_oat(key, key_len);
  return oconst_lookup(ketama, hash);
}

void omcache_dist_ketama_free(void *ketama)
{
  oconst_free(ketama);
}

omcache_server_t *omcache_dist_modulo_lookup(const unsigned char *key, size_t key_len, void *context)
{
  omcache_t *mc = (omcache_t *) context;
  uint32_t hash = omcache_hash_jenkins_oat(key, key_len);
  return mc->servers[hash % mc->server_count];
}

static int omcache_srv_connect(omcache_t *mc, omcache_server_t *srv)
{
  if (srv->sock == -1)
    {
      int64_t now = omcache_msec(mc);
      int err;

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
          err = getaddrinfo(srv->hostname, srv->port, &hints, &srv->addrs);
          if (err != 0)
            {
              omcache_srv_log(srv, "getaddrinfo: %s", gai_strerror(err));
              srv->addrs = NULL;
              return OMCACHE_FAIL;
            }
          srv->addrp = srv->addrs;
          srv->last_gai = now;
        }
      while (srv->addrp)
        {
          int sock = socket(srv->addrp->ai_family, srv->addrp->ai_socktype, srv->addrp->ai_protocol);
          fcntl(sock, F_SETFL, O_NONBLOCK);
          err = connect(sock, srv->addrp->ai_addr, srv->addrp->ai_addrlen);
          srv->addrp = srv->addrp->ai_next;
          if (err == 0)
            {
              srv->sock = sock;
              srv->conn_timeout = 0;
              break;
            }
          else if (errno == EINPROGRESS)
            {
              srv->sock = sock;
              srv->conn_timeout = now + mc->conn_timeout_msec;
              omcache_srv_log(srv, "%s", "connection in progress");
              return OMCACHE_AGAIN;
            }
          else
            {
              omcache_srv_log(srv, "connect failed: %s", strerror(errno));
              close(sock);
            }
        }
      freeaddrinfo(srv->addrs);
      srv->addrs = NULL;
      if (srv->sock == -1)
        {
          omcache_srv_log(srv, "%s", "failed to connect");
          return OMCACHE_FAIL;
        }
    }
  else if (srv->conn_timeout > 0)
    {
      struct pollfd pfd = { .fd = srv->sock, .events = POLLOUT, .revents = 0, };
      if (poll(&pfd, 1, 0) == 0)
        {
          if (omcache_msec(mc) >= srv->conn_timeout)
            {
              // timeout
              close(srv->sock);
              srv->sock = -1;
              omcache_srv_log(srv, "%s", "timeout");
            }
          return OMCACHE_AGAIN;
        }
      int err;
      socklen_t err_len = sizeof(err);
      if (getsockopt(srv->sock, SOL_SOCKET, SO_ERROR, &err, &err_len) == -1)
        {
          omcache_srv_log(srv, "getsockopt failed: %s", strerror(errno));
          err = SO_ERROR;
        }
      if (err)
        {
          close(srv->sock);
          srv->sock = -1;
          omcache_srv_log(srv, "connect failed: %s", strerror(err));
          return OMCACHE_AGAIN;
        }
      omcache_srv_log(srv, "%s", "connected");
      srv->conn_timeout = 0;
    }
  return OMCACHE_OK;
}

static int omcache_srv_read(omcache_t *mc, omcache_server_t *srv,
                            omcache_resp_t *resps, size_t *resp_cnt)
{
  // make sure we have room for some data
  size_t space = srv->recv_buffer.end - srv->recv_buffer.w;
  if (space < 0xFFFF)
    {
      size_t buffered = srv->recv_buffer.w - srv->recv_buffer.r;
      if (srv->recv_buffer.r != srv->recv_buffer.base)
        {
          memmove(srv->recv_buffer.base, srv->recv_buffer.r, buffered);
        }
      space = srv->recv_buffer.end - srv->recv_buffer.w;
      if (space < 0xFFFF)
        {
          srv->recv_buffer.base = realloc(srv->recv_buffer.base, buffered + 0xFFFF * 4);
          srv->recv_buffer.end = srv->recv_buffer.base + buffered + 0xFFFF * 4;
        }
      srv->recv_buffer.r = srv->recv_buffer.base;
      srv->recv_buffer.w = srv->recv_buffer.base + buffered;
    }
  ssize_t res = read(srv->sock, srv->recv_buffer.w, srv->recv_buffer.end - srv->recv_buffer.w);
  omcache_srv_log(srv, "read %zd bytes", res);
  if (res > 0)
    {
      srv->recv_buffer.w += res;
    }
  // try to handle as many messages as possible
  ssize_t i, iov_max = (resps && resp_cnt) ? (ssize_t) *resp_cnt : -1;
  for (i = 0; (i < iov_max) || (iov_max == -1); i++)
    {
      protocol_binary_response_header *hdr =
        (protocol_binary_response_header *) srv->recv_buffer.r;
      size_t buffered = srv->recv_buffer.w - srv->recv_buffer.r;
      size_t msg_size = sizeof(protocol_binary_response_header);
      if (buffered < msg_size)
        {
          omcache_srv_log(srv, "not enough data in buffer (%zd, need %zd)", buffered, msg_size);
          break;
        }
      // check body length (but don't overwrite it in the buffer yet)
      size_t body_size = be32toh(hdr->response.bodylen);
      msg_size += body_size;
      if (buffered < msg_size)
        {
          omcache_srv_log(srv, "not enough data in buffer (%zd, need %zd)", buffered, msg_size);
          break;
        }
      hdr->response.bodylen = body_size;
      hdr->response.keylen = be16toh(hdr->response.keylen);
      hdr->response.status = be16toh(hdr->response.status);
      if (hdr->response.opaque)
        {
          srv->last_req_recvd = hdr->response.opaque;
        }
      if (resps && resp_cnt)
        {
          resps[i].header = hdr;
          resps[i].key = srv->recv_buffer.r + sizeof(hdr->bytes) + hdr->response.extlen;
          resps[i].data = resps[i].key + hdr->response.keylen;
        }
      omcache_srv_log(srv, "received message: type %x, id %u",
                      (unsigned int) hdr->response.opcode, hdr->response.opaque);
      srv->recv_buffer.r += msg_size;
    }
  if (resps && resp_cnt)
    {
      *resp_cnt = i;
    }
  // reset read buffer in case everything was processed
  if (srv->recv_buffer.r == srv->recv_buffer.w)
    {
      srv->recv_buffer.r = srv->recv_buffer.base;
      srv->recv_buffer.w = srv->recv_buffer.base;
    }
  return (srv->last_req_recvd == srv->last_req_sent) ? OMCACHE_OK : OMCACHE_AGAIN;
}

static int omcache_srv_io(omcache_t *mc, omcache_server_t *srv,
                          omcache_resp_t *resps, size_t *resp_cnt,
                          bool force_connect)
{
  ssize_t buf_len = srv->send_buffer.w - srv->send_buffer.r;
  int err, again = 0;

  if (buf_len > 0 || force_connect)
    {
      err = omcache_srv_connect(mc, srv);
      if (err != OMCACHE_OK)
        {
          if (resp_cnt)
            {
              *resp_cnt = 0;
            }
          return err;
        }
    }
  if (buf_len > 0)
    {
      ssize_t r = write(srv->sock, srv->send_buffer.r, buf_len);
      if (r <= 0)
        {
          return -1;
        }
      srv->send_buffer.r += r;
      buf_len -= r;
      // reset send buffer in case everything was written
      if (buf_len > 0)
        {
          again ++;
        }
      else
        {
          srv->send_buffer.r = srv->send_buffer.base;
          srv->send_buffer.w = srv->send_buffer.base;
        }
    }
  if (srv->conn_timeout == 0 && srv->sock >= 0)
    {
      // omcache_srv_log(srv, "begin handling, last_req_sent: %u, last_req_recvd: %u",
      //                 srv->last_req_sent, srv->last_req_recvd);
      err = omcache_srv_read(mc, srv, resps, resp_cnt);
      // omcache_srv_log(srv, "end handling (%d), last_req_sent: %u, last_req_recvd: %u",
      //                 err, srv->last_req_sent, srv->last_req_recvd);
      if (err == OMCACHE_AGAIN)
        {
          again ++;
        }
      else if (err != OMCACHE_OK)
        {
          return err;
        }
    }
  return again ? OMCACHE_AGAIN : OMCACHE_OK;
}

static int omcache_io(omcache_t *mc, int timeout_msec,
                      omcache_resp_t *resps, size_t *resp_cnt)
{
  omcache_enter(mc);
  int again, ret = OMCACHE_OK;
  int64_t now = omcache_msec(mc), timeout_abs = -1;
  omcache_resp_t *resp_ptr = resps;
  size_t resps_total = 0, resps_size = resp_cnt ? *resp_cnt : 0;

  if (timeout_msec > 0)
    {
      timeout_abs = now + timeout_msec;
    }

  while (ret == OMCACHE_OK)
    {
      if (timeout_abs >= 0)
        {
          omcache_enter(mc);
          now = omcache_msec(mc);
          if (now > timeout_abs)
            {
              return OMCACHE_AGAIN;
            }
          timeout_msec = timeout_abs - now;
        }

      int nfds = -1;
      struct pollfd *pfds = omcache_poll_fds(mc, &nfds);
      if (nfds == 0)
        {
          return OMCACHE_OK;
        }
      int polls = poll(pfds, nfds, timeout_msec);
      if (polls == 0)
        {
          omcache_log("poll: timeout (%lld)", timeout_msec);
          return OMCACHE_AGAIN;
        }
      omcache_log("poll: %d", polls);

      again = 0;
      for (off_t i=0; (i<mc->server_count) && (ret == OMCACHE_OK); i++)
        {
          if (mc->servers[i])
            {
              size_t resps_received = resps_size - resps_total;
              ret = omcache_srv_io(mc, mc->servers[i],
                                   resp_ptr, &resps_received,
                                   false);
              omcache_srv_log(mc->servers[i], "io: %s / %zu",
                              omcache_strerror(mc, ret), resps_received);
              if (ret == OMCACHE_AGAIN)
                {
                  again ++;
                  ret = OMCACHE_OK;
                }
              if (resps_received > 0)
                {
                  resp_ptr += resps_received;
                  resps_total += resps_received;
                  *resp_cnt = resps_total;
                }
              if (resps_size > 0 && resps_total == resps_size)
                {
                  again ++;
                  break;
                }
            }
        }

      // break the loop in case we didn't want to poll or we read something:
      // the read buffer may be overwritten by subsequent calls to
      // omcache_srv_io so we must let our caller process the already
      // received events before calling it again.
      if ((timeout_msec == 0) || (resps_total > 0))
        {
          break;
        }
    }
  return (ret == OMCACHE_OK && again > 0) ? OMCACHE_AGAIN : ret;
}

int omcache_flush_buffers(omcache_t *mc, int timeout_msec)
{
  return omcache_io(mc, timeout_msec, NULL, NULL);
}

int omcache_set_buffering(omcache_t *mc, unsigned int enabled)
{
  mc->buffer_writes = enabled ? 1 : 0;
  return OMCACHE_OK;
}

int omcache_reset_buffering(omcache_t *mc)
{
  for (off_t i=0; i<mc->server_count; i++)
    {
      omcache_server_t *srv = mc->servers[i];
      srv->send_buffer.r = srv->send_buffer.base;
      srv->send_buffer.w = srv->send_buffer.base;
      srv->recv_buffer.r = srv->recv_buffer.base;
      srv->recv_buffer.w = srv->recv_buffer.base;
      srv->last_req_recvd = srv->last_req_sent;
    }
  return OMCACHE_OK;
}

static unsigned int omcache_is_request_quiet(uint8_t opcode)
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
    case 0x1e: // PROTOCOL_BINARY_CMD_GATQ:
    case 0x24: // PROTOCOL_BINARY_CMD_GATKQ:
    case PROTOCOL_BINARY_CMD_RSETQ:
    case PROTOCOL_BINARY_CMD_RAPPENDQ:
    case PROTOCOL_BINARY_CMD_RPREPENDQ:
    case PROTOCOL_BINARY_CMD_RDELETEQ:
    case PROTOCOL_BINARY_CMD_RINCRQ:
    case PROTOCOL_BINARY_CMD_RDECRQ:
      return 1;
    }
  return 0;
}

static unsigned int omcache_is_request_keyless(uint8_t opcode)
{
  switch (opcode)
    {
    case PROTOCOL_BINARY_CMD_NOOP:
    case PROTOCOL_BINARY_CMD_VERSION:
    case PROTOCOL_BINARY_CMD_STAT:
      return 1;
    }
  return 0;
}

static int omcache_srv_write(omcache_t *mc, omcache_server_t *srv,
                             struct iovec *iov, size_t iov_cnt)
{
  ssize_t buf_len = srv->send_buffer.w - srv->send_buffer.r;
  ssize_t res = 0, msg_len = 0;
  size_t i;

  for (i=0; i<iov_cnt; i++)
    {
      msg_len += iov[i].iov_len;
    }
  if ((size_t) (buf_len + msg_len) > mc->send_buffer_max)
    {
      return OMCACHE_BUFFER_FULL;
    }

  // set last_req_sent field now that we're about to send (or buffer) this
  protocol_binary_request_header *hdr = iov[0].iov_base;
  srv->last_req_sent = hdr->request.opaque;
  srv->last_req_quiet = omcache_is_request_quiet(hdr->request.opcode);

  // make sure we're meant to write immediately and the connection is
  // established and the existing write buffer empty
  if (mc->buffer_writes == 0 && omcache_srv_io(mc, srv, NULL, NULL, true) == OMCACHE_OK)
    {
      res = writev(srv->sock, iov, iov_cnt);
    }
  if (res == msg_len)
    {
      return OMCACHE_OK;
    }

  // buffer everything we didn't write
  if (srv->send_buffer.end - srv->send_buffer.w < msg_len)
    {
      buf_len = srv->send_buffer.w - srv->send_buffer.r;
      if (buf_len > 0)
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

static int omcache_srv_send_noop(omcache_t *mc, omcache_server_t *srv)
{
  protocol_binary_request_noop req;
  memset(&req, 0, sizeof(req));

  req.message.header.request.magic = PROTOCOL_BINARY_REQ;
  req.message.header.request.opcode = PROTOCOL_BINARY_CMD_NOOP;
  req.message.header.request.datatype = PROTOCOL_BINARY_RAW_BYTES;
  req.message.header.request.opaque = ++ mc->req_id;
  struct iovec iov[] = {{ .iov_len = sizeof(req.bytes), .iov_base = req.bytes }};
  return omcache_srv_write(mc, srv, iov, 1);
}

int omcache_write(omcache_t *mc, omcache_req_t *req)
{
  omcache_enter(mc);
  protocol_binary_request_header *hdr = (protocol_binary_request_header *) req->header;
  // set the common magic numbers for request
  hdr->request.magic = PROTOCOL_BINARY_REQ;
  hdr->request.datatype = PROTOCOL_BINARY_RAW_BYTES;
  // set an incrementing request id to each request
  hdr->request.opaque = ++ mc->req_id;
  // the request must always include a key even if the request type doesn't
  // actually use a key (for example NOOP, VERSION and STATS) so we use it
  // here and then zero the keylen in case it's not used.
  size_t h_bodylen = be32toh(hdr->request.bodylen),
         h_keylen = be16toh(hdr->request.keylen),
         h_datalen = h_bodylen - h_keylen - hdr->request.extlen,
         lookup_keylen = h_keylen;
  if (omcache_is_request_keyless(hdr->request.opcode))
    {
      hdr->request.bodylen = htobe32(h_bodylen - h_keylen);
      hdr->request.keylen = h_keylen = 0;
    }

  // construct the iovector
  struct iovec iov[] = {
    { .iov_base = (void *) hdr, .iov_len = sizeof(hdr->bytes) + hdr->request.extlen, },
    { .iov_base = (void *) req->key, .iov_len = h_keylen, },
    { .iov_base = (void *) req->data, .iov_len = h_datalen, },
    };

  // handle server selection and replication
  if (mc->server_count == 1)
    {
      return omcache_srv_write(mc, mc->servers[0], iov, 3);
    }

  int i, srv_i, replicas = mc->replicate_writes + 1, ret = OMCACHE_OK;
  if (replicas == 0 || replicas >= mc->server_count)
    {
      replicas = mc->server_count;
      srv_i = 0;
    }
  else
    {
      omcache_server_t *srv = mc->dist_lookup(req->key, lookup_keylen, mc->dist_context);
      if (replicas == 1)
        {
          return omcache_srv_write(mc, srv, iov, 3);
        }
      srv_i = srv->list_index;
    }
  for (i=srv_i; (i < srv_i + replicas) && (ret == OMCACHE_OK || ret == OMCACHE_BUFFERED); i++)
    {
      ret = omcache_srv_write(mc, mc->servers[i % mc->server_count], iov, 3);
    }
  return ret;
}

int omcache_set_replication(omcache_t *mc, int replicas)
{
  mc->replicate_writes = max(replicas, -1);
  return OMCACHE_OK;
}

int omcache_read(omcache_t *mc, omcache_resp_t *resps, size_t *resp_cnt, int timeout_msec)
{
  return omcache_io(mc, timeout_msec, resps, resp_cnt);
}

int omcache_command(omcache_t *mc, omcache_req_t *req, omcache_resp_t *resp, int timeout_msec)
{
  int ret = omcache_write(mc, req);
  if (resp == NULL)
    {
      return ret;
    }
  // look for the response to the query we just sent (ignore all others)
  int64_t now = omcache_msec(mc);
  int64_t timeout_abs = (timeout_msec > 0) ? now + timeout_msec : -1;
  while (timeout_abs == -1 || now < timeout_abs)
    {
      size_t resps = 1;
      ret = omcache_io(mc, (timeout_abs > 0) ? timeout_abs - now : timeout_msec, resp, &resps);
      if (ret == OMCACHE_OK && resps == 1)
        {
          protocol_binary_response_header *resp_hdr =
              (protocol_binary_response_header *) resp->header;
          if (resp_hdr->response.opaque == mc->req_id)
            {
              break;
            }
        }
      if (ret != OMCACHE_AGAIN)
        {
          break;
        }
      omcache_enter(mc);
      now = omcache_msec(mc);
    }
  return ret;
}

int omcache_noop(omcache_t *mc,
                 const unsigned char *key_for_server_selection,
                 size_t key_len)
{
  protocol_binary_request_noop req = {.bytes = {0}};
  req.message.header.request.opcode = PROTOCOL_BINARY_CMD_NOOP;
  req.message.header.request.keylen = htobe16(key_len);
  req.message.header.request.bodylen = htobe32(key_len);
  omcache_req_t oreq = {.header = req.bytes, .key = key_for_server_selection, .data = NULL};
  omcache_resp_t oresp;
  return omcache_command(mc, &oreq, &oresp, -1);
}

int omcache_stat(omcache_t *mc,
                 const unsigned char *key_for_server_selection,
                 size_t key_len)
{
  protocol_binary_request_noop req = {.bytes = {0}};
  req.message.header.request.opcode = PROTOCOL_BINARY_CMD_STAT;
  req.message.header.request.keylen = htobe16(key_len);
  req.message.header.request.bodylen = htobe32(key_len);
  omcache_req_t oreq = {.header = req.bytes, .key = key_for_server_selection, .data = NULL};
  omcache_resp_t oresp;
  return omcache_command(mc, &oreq, &oresp, -1);
}

static int omcache_set_cmd(omcache_t *mc, protocol_binary_command opcode,
                           const unsigned char *key, size_t key_len,
                           const unsigned char *value, size_t value_len,
                           time_t expiration, unsigned int flags)
{
  size_t ext_len = 8;
  protocol_binary_request_set req = {.bytes = {0}};
  req.message.header.request.opcode = opcode;
  req.message.header.request.extlen = ext_len;
  req.message.header.request.keylen = htobe16((uint16_t) key_len);
  req.message.header.request.bodylen = htobe32(key_len + value_len + ext_len);
  req.message.body.flags = htobe32(flags);
  req.message.body.expiration = htobe32((uint32_t) expiration);
  omcache_req_t oreq = {.header = req.bytes, .key = key, .data = value};
  return omcache_write(mc, &oreq);
}

#define QCMD(k) mc->buffer_writes ? k ## Q : k

int omcache_set(omcache_t *mc,
                const unsigned char *key, size_t key_len,
                const unsigned char *value, size_t value_len,
                time_t expiration, unsigned int flags)
{
  return omcache_set_cmd(mc, QCMD(PROTOCOL_BINARY_CMD_SET),
                         key, key_len, value, value_len,
                         expiration, flags);
}

int omcache_add(omcache_t *mc,
                const unsigned char *key, size_t key_len,
                const unsigned char *value, size_t value_len,
                time_t expiration, unsigned int flags)
{
  return omcache_set_cmd(mc, QCMD(PROTOCOL_BINARY_CMD_ADD),
                         key, key_len, value, value_len,
                         expiration, flags);
}

int omcache_replace(omcache_t *mc,
                    const unsigned char *key, size_t key_len,
                    const unsigned char *value, size_t value_len,
                    time_t expiration, unsigned int flags)
{
  return omcache_set_cmd(mc, QCMD(PROTOCOL_BINARY_CMD_REPLACE),
                         key, key_len, value, value_len,
                         expiration, flags);
}

static int omcache_ctr_cmd(omcache_t *mc, protocol_binary_command opcode,
                           const unsigned char *key, size_t key_len,
                           uint64_t delta, uint64_t initial,
                           time_t expiration)
{
  size_t ext_len = 8 + 8 + 4;
  protocol_binary_request_incr req = {.bytes = {0}};
  req.message.header.request.opcode = opcode;
  req.message.header.request.extlen = ext_len;
  req.message.header.request.keylen = htobe16((uint16_t) key_len);
  req.message.header.request.bodylen = htobe32(key_len + ext_len);
  req.message.body.delta = htobe64(delta);
  req.message.body.initial = htobe64(initial);
  req.message.body.expiration = htobe32((uint32_t) expiration);
  omcache_req_t oreq = {.header = req.bytes, .key = key, .data = NULL};
  return omcache_write(mc, &oreq);
 }

int omcache_increment(omcache_t *mc,
                      const unsigned char *key, size_t key_len,
                      uint64_t delta, uint64_t initial,
                      time_t expiration)
{
  return omcache_ctr_cmd(mc, QCMD(PROTOCOL_BINARY_CMD_INCREMENT),
                         key, key_len, delta, initial, expiration);
}

int omcache_decrement(omcache_t *mc,
                      const unsigned char *key, size_t key_len,
                      uint64_t delta, uint64_t initial,
                      time_t expiration)
{
  return omcache_ctr_cmd(mc, QCMD(PROTOCOL_BINARY_CMD_DECREMENT),
                         key, key_len, delta, initial, expiration);
}

int omcache_delete(omcache_t *mc,
                   const unsigned char *key, size_t key_len)
{
  protocol_binary_request_delete req = {.bytes = {0}};
  req.message.header.request.opcode = QCMD(PROTOCOL_BINARY_CMD_DELETE);
  req.message.header.request.keylen = htobe16((uint16_t) key_len);
  req.message.header.request.bodylen = htobe32((uint32_t) key_len);
  omcache_req_t oreq = {.header = req.bytes, .key = key, .data = NULL};
  return omcache_write(mc, &oreq);
}
