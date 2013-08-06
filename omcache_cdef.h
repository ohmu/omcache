/*
 * omcache_cdef.h - bare c function definitions
 * for omcache.h and python cffi
 *
 * Copyright (c) 2013, Oskari Saarenmaa <os@ohmu.fi>
 * All rights reserved.
 *
 * This file is under the 2-clause BSD license.
 * See the file `LICENSE` for details.
 *
 */

typedef enum omcache_ret_e {
  OMCACHE_OK = 0,
  OMCACHE_FAIL,
  OMCACHE_INVALID,
  OMCACHE_AGAIN,
  OMCACHE_BUFFERED,
  OMCACHE_BUFFER_FULL,
  OMCACHE_NOT_FOUND,
} omcache_ret_t;

typedef struct omcache_s omcache_t;
typedef struct omcache_server_s omcache_server_t;

// OMcache -object
omcache_t *omcache_init(void);
int omcache_free(omcache_t *mc);
const char *omcache_strerror(omcache_t *mc, int rc);

// Settings
int omcache_set_conn_timeout(omcache_t *mc, unsigned int msec);
int omcache_set_send_buffer_max_size(omcache_t *mc, size_t size);
int omcache_set_servers(omcache_t *mc, const char *servers);

typedef void *(omcache_dist_init_func)(omcache_server_t **servers, size_t server_count, void *context);
typedef void (omcache_dist_free_func)(void *context);
typedef omcache_server_t *(omcache_dist_lookup_func)(const unsigned char *key, size_t key_len, void *context);
void *omcache_dist_ketama_init(omcache_server_t **servers, size_t server_count, void *mc);
void omcache_dist_ketama_free(void *ketama);
omcache_server_t *omcache_dist_ketama_lookup(const unsigned char *key, size_t key_len, void *ketama);
omcache_server_t *omcache_dist_modulo_lookup(const unsigned char *key, size_t key_len, void *mc);
int omcache_set_dist_func(omcache_t *mc,
                          omcache_dist_init_func *init_func,
                          omcache_dist_free_func *free_func,
                          omcache_dist_lookup_func *lookup_func,
                          void *init_context);

typedef void (omcache_log_func)(void *context, int level, const char *fmt, ...);
void omcache_log_stderr(void *context, int level, const char *fmt, ...);
int omcache_set_log_func(omcache_t *mc, omcache_log_func *func, void *context);

// Actions
struct pollfd *omcache_poll_fds(omcache_t *mc, int *nfds);
int omcache_flush_buffers(omcache_t *mc, int64_t timeout_msec);
int omcache_write(omcache_t *mc, struct iovec *iov, size_t iov_cnt);
int omcache_read(omcache_t *mc, struct iovec *iov, size_t iov_cnt);

// Commands
int omcache_noop(omcache_t *mc, const unsigned char *key_for_server_selection, size_t key_len);
int omcache_set(omcache_t *mc,
                const unsigned char *key, size_t key_len,
                const unsigned char *value, size_t value_len,
                time_t expiration, unsigned int flags);
int omcache_add(omcache_t *mc,
                const unsigned char *key, size_t key_len,
                const unsigned char *value, size_t value_len,
                time_t expiration, unsigned int flags);
int omcache_replace(omcache_t *mc,
                    const unsigned char *key, size_t key_len,
                    const unsigned char *value, size_t value_len,
                    time_t expiration, unsigned int flags);
int omcache_increment(omcache_t *mc,
                      const unsigned char *key, size_t key_len,
                      uint64_t delta, uint64_t initial,
                      time_t expiration);
int omcache_decrement(omcache_t *mc,
                      const unsigned char *key, size_t key_len,
                      uint64_t delta, uint64_t initial,
                      time_t expiration);
int omcache_delete(omcache_t *mc,
                   const unsigned char *key, size_t key_len);
