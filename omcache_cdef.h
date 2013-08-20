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

typedef struct omcache_req_s {
    void *header;
    const unsigned char *key;
    const unsigned char *data;
} omcache_req_t;

typedef struct omcache_resp_s {
    void *header;
    const unsigned char *key;
    const unsigned char *data;
} omcache_resp_t;

// OMcache -object

/**
 * Create a new OMcache handle.
 * @return A new OMcache handle that can be used with other functions.
 */
omcache_t *omcache_init(void);

/**
 * Free an OMcache handle and its resources.
 * @param mc OMcache handle.
 * @return OMCACHE_OK on success.
 */
int omcache_free(omcache_t *mc);

/**
 * Human readable message for an OMcache error code.
 * @param mc OMcache handle.
 * @param rc OMcache return code.
 * @return Human readable NUL-terminated string (managed by OMcache).
 */
const char *omcache_strerror(omcache_t *mc, int rc);

// Settings

/**
 * Set OMcache handle's connection timeout.
 * @param mc OMcache handle.
 * @param msec Maximum time to wait for a connection.
 * @return OMCACHE_OK on success.
 */
int omcache_set_conn_timeout(omcache_t *mc, unsigned int msec);

/**
 * Set OMcache handle's maximum buffer size.
 * @param mc OMcache handle.
 * @param size Maximum number of bytes to buffer for writes and reads.
 * @return OMCACHE_OK on success.
 */
int omcache_set_send_buffer_max_size(omcache_t *mc, size_t size);

/**
 * Set OMcache handle's buffering mode.
 * @param mc OMcache handle.
 * @param enabled If non-zero, all server operations will be buffered
 *                and won't be flushed to the backend before
 *                omcache_flush_buffers is called.
 * @return OMCACHE_OK on success.
 */
int omcache_set_buffering(omcache_t *mc, unsigned int enabled);

/**
 * Set the server(s) to use with an OMcache handle.
 * @param mc OMcache handle.
 * @param servers Comma-separated list of memcached servers.
 * @return OMCACHE_OK on success.
 */
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

/**
 * Populate a struct polld array with required actions.
 * @param mc OMcache handle.
 * @param nfds Pointer to an integer signifying the number of file
 *             descriptors that were returned.
 * @return A struct pollfd array (allocated and managed by OMcache).
 */
struct pollfd *omcache_poll_fds(omcache_t *mc, int *nfds);

/**
 * Attempt to flush all unwritten bytes to the servers.
 * @param mc OMcache handle.
 * @param timeout_msec Maximum number of milliseconds to block while flushing
 *                     the buffers.  Zero means no blocking at all and a
 *                     negative value blocks indefinitely until all buffers
 *                     have been flushed.
 * @return OMCACHE_OK on success; OMCACHE_AGAIN in case of timeout.
 */
int omcache_flush_buffers(omcache_t *mc, int timeout_msec);

/**
 * Clear all OMcache buffers.
 * @param mc OMcache handle.
 * @return OMCACHE_OK on success.
 */
int omcache_reset_buffering(omcache_t *mc);

/**
 * Set OMcache's replication factor.
 * @param mc OMcache handle.
 * @param replicas Number of servers to write to in addition to the server
 *                 selected by key.  Negative replication factor causes
 *                 writes to be replicated to all servers.
 * @return OMCACHE_OK on success; OMCACHE_AGAIN in case of timeout.
 */
int omcache_set_replication(omcache_t *mc, int replicas);

/**
 * Write (or buffer) a message to a server selected using the key.
 * @param mc OMcache handle.
 * @param req An omcache_req_t struct containing at least the request header
 *            and key and optionally data.  Note that the key must be always
 *            present in the request even if the request type does not use a
 *            key (for example NOOP, VERSION and STATS); in that case the
 *            key is removed before transmission and request keylen and
 *            bodylen are adjusted accordingly.
 * @return OMCACHE_OK if data was successfully written;
 *         OMCACHE_BUFFERED if data was successfully added to write buffer;
 *         OMCACHE_BUFFER_FULL if buffer was full and data was not written.
 */
int omcache_write(omcache_t *mc, omcache_req_t *req);

/**
 * Read responses from servers.
 * @param mc OMcache handle.
 * @param resps Pointer to an array of omcache_resp_t structures, on return
 *              each structure will contain one memcached server response.
 *              The memory may be overwritten on subsequent calls to
 *              omcache_read and must not be modified or freed by caller.
 * @param resp_cnt Pointer containing the number of entries in resps.  The
 *                 pointer will contain the number of successfully received
 *                 responses on return.
 * @param timeout_msec Maximum number of milliseconds to block while waiting
 *                     for the responses.  Zero means no blocking at all and
 *                     a negative value blocks indefinitely until at least
 *                     one message has been received.
 * @return OMCACHE_OK If all responses were received;
 *         OMCACHE_AGAIN If there is more data to read.
*/
int omcache_read(omcache_t *mc, omcache_resp_t *resps, size_t *resp_cnt, int timeout_msec);

int omcache_command(omcache_t *mc, omcache_req_t *req, omcache_resp_t *resp, int timeout_msec);

// Commands
int omcache_noop(omcache_t *mc,
                 const unsigned char *key_for_server_selection,
                 size_t key_len);
int omcache_stat(omcache_t *mc,
                 const unsigned char *key_for_server_selection,
                 size_t key_len);
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
