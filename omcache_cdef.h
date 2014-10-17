/*
 * omcache_cdef.h - bare c function definitions
 * for omcache.h and python cffi
 *
 * Copyright (c) 2013-2014, Oskari Saarenmaa <os@ohmu.fi>
 * All rights reserved.
 *
 * This file is under the Apache License, Version 2.0.
 * See the file `LICENSE` for details.
 *
 */

// Note: some of these numbers try to match the identifiers used by the
// memcached binary protocol
typedef enum omcache_ret_e {
  OMCACHE_OK = 0x0000, ///< success
  OMCACHE_NOT_FOUND = 0x0001, ///< key not found from memcached
  OMCACHE_KEY_EXISTS = 0x0002, ///< conflicting key exists in memcached
  OMCACHE_DELTA_BAD_VALUE = 0x0006, ///< value can not be incremented or decremented
  OMCACHE_FAIL = 0x0999, ///< memcached signaled command failure
  OMCACHE_AGAIN = 0x1001, ///< operation would block, call again
  OMCACHE_BUFFERED, ///< data buffered internally, request not delivered yet
  OMCACHE_BUFFER_FULL, ///< internal buffers full, can not process request
  OMCACHE_NO_SERVERS, ///< no server available for communication
  OMCACHE_SERVER_FAILURE, ///< failure communicating to selected server
} omcache_ret_t;

typedef struct omcache_s omcache_t;

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
 * @param rc OMcache return code.
 * @return Human readable NUL-terminated string (managed by OMcache).
 */
const char *omcache_strerror(int rc);

// Settings

/**
 * Set OMcache handle's connection timeout.
 * @param mc OMcache handle.
 * @param msec Maximum milliseconds to wait for a connection.
 * @return OMCACHE_OK on success.
 */
int omcache_set_connect_timeout(omcache_t *mc, uint32_t msec);

/**
 * Set OMcache handle's reconnect-after-failure timeout.
 * @param mc OMcache handle.
 * @param msec Number of milliseconds to wait before attempting to
 *             reconnect to a failed node.
 * @return OMCACHE_OK on success.
 */
int omcache_set_reconnect_timeout(omcache_t *mc, uint32_t msec);

/**
 * Set OMcache handle's io operation timeout.
 * @param mc OMcache handle.
 * @param msec Number of milliseconds to wait until IO operations
 *             finish before declaring the node dead.
 * @return OMCACHE_OK on success.
 */
int omcache_set_dead_timeout(omcache_t *mc, uint32_t msec);

/**
 * Set OMcache handle's maximum buffer size for outgoing messages.
 * @param mc OMcache handle.
 * @param size Maximum number of bytes to buffer for writes.
 *             Note that this is maximum buffer size per server.
 *             Default is 10 megabytes.
 * @return OMCACHE_OK on success.
 */
int omcache_set_send_buffer_max_size(omcache_t *mc, size_t size);

/**
 * Set OMcache handle's maximum buffer size for incoming messages.
 * @param mc OMcache handle.
 * @param size Maximum number of bytes to buffer for reads.
 *             Note that this is maximum buffer size per server.
 *             Default is 1056 kilobytes which is enough to handle
 *             a response of the maximum default size (1MB) but if
 *             memcached is run with a different maximum value length
 *             setting this setting should be adjusted as well.
 * @return OMCACHE_OK on success.
 */
int omcache_set_recv_buffer_max_size(omcache_t *mc, size_t size);

/**
 * Set OMcache handle's buffering mode.
 * @param mc OMcache handle.
 * @param enabled If non-zero, all server operations will be buffered
 *                and won't be flushed to the backend before
 *                omcache_io is called.
 * @return OMCACHE_OK on success.
 */
int omcache_set_buffering(omcache_t *mc, uint32_t enabled);

/**
 * Set the server(s) to use with an OMcache handle.
 * @param mc OMcache handle.
 * @param servers Comma-separated list of memcached servers.
 *                Any existing servers on OMcache's server list that do not
 *                appear on the new list are dropped.  The servers that
 *                appear on both the currently used and new lists are kept
 *                and connections to them are not reset.
 * @return OMCACHE_OK on success.
 */
int omcache_set_servers(omcache_t *mc, const char *servers);

/**
 * Log callback function type.
 * @param context Opaque context set in omcache_set_log_callback()
 * @param level Log message level; levels are defined in <sys/syslog.h>.
 * @param msg The actual log message.
 */
typedef void (omcache_log_callback_func)(void *context, int level, const char *msg);

/**
 * Built-in logging function to log to standard error.
 * @param context Opaque context (unused in this function).
 * @param level Log message level; levels are defined in <sys/syslog.h>.
 * @param msg The actual log message.
 */
void omcache_log_stderr(void *context, int level, const char *msg);

/**
 * Set a log callback for the OMcache handle.
 * @param mc OMcache handle.
 * @param func Callback function to call for each generated log message.
 * @param resp_cb_context Opaque context to pass to the callback function.
 * @return OMCACHE_OK on success.
 */
int omcache_set_log_callback(omcache_t *mc, omcache_log_callback_func *func, void *context);

/**
 * Response callback type.
 * @param mc OMcache handle.
 * @param res Response status (see omcache_ret_t).
 * @param resp Response object.
 * @param context Opaque context set in omcache_set_response_callback().
 */
typedef void (omcache_response_callback_func)(omcache_t *mc, int res, omcache_resp_t *resp, void *context);

/**
 * Register a callback function for memcached responses.
 * @param mc OMcache handle.
 * @param resp_cb Callback function to call for all responses to requests.
 * @param resp_cb_context Opaque context to pass to the callback function.
 * @return OMCACHE_OK on success.
 */
int omcache_set_response_callback(omcache_t *mc, omcache_response_callback_func *resp_cb, void *resp_cb_context);

// Control

/**
 * Populate a struct polld array with required actions.
 * @param mc OMcache handle.
 * @param nfds Pointer to an integer signifying the number of file
 *             descriptors that were returned.
 * @param poll_timeout Maximum timeout that should be used in poll.
 * @return A struct pollfd array (allocated and managed by OMcache).
 */
struct pollfd *omcache_poll_fds(omcache_t *mc, int *nfds, int *poll_timeout);

/**
 * Clear all OMcache buffers.
 * @param mc OMcache handle.
 * @return OMCACHE_OK on success.
 */
int omcache_reset_buffers(omcache_t *mc);

/**
 * Perform I/O with memcached servers: establish connections, read
 * responses, write requests.
 * @param mc OMcache handle.
 * @param timeout_msec Maximum number of milliseconds to block while waiting
 *                     for I/O to complete.  Zero means no blocking at all
 *                     and a negative value blocks indefinitely until at
 *                     least one message has been received.
 * @param req_id Request id to look for in responses, once a response to this
 *               request has been received the function returns.
 * @param resp Pointer to a omcache_resp_t structure to store the response
 *             pointed by req_id into.  The memory will be overwritten on
 *             subsequent calls to omcache_io and must not be modified or
 *             freed by caller.
 * @return OMCACHE_OK If all responses were received;
 *         OMCACHE_AGAIN If there is more data to read.
*/
int omcache_io(omcache_t *mc, int32_t timeout_msec, uint32_t req_id, omcache_resp_t *resp);

/**
 * Send a request to memcache and optionally read a response.
 * @param mc OMcache handle.
 * @param req An omcache_req_t struct containing at the request header
 *            and optionally a key and data.
 * @param resp An optional response structure to be filled with the response
 *             to the sent request.
 * @param server_index Opaque integer identifying the server to use when
 *                     the request type does not use a key (for example NOOP,
 *                     VERSION and STATS). -1 when server is selected by key.
 * @param timeout_msec Maximum number of milliseconds to block while waiting
 *                     for the response.  Zero means no blocking at all and
 *                     a negative value blocks indefinitely until at a
 *                     response is received or an error occurs.
 * @return OMCACHE_OK if data was successfully written;
 *         OMCACHE_BUFFERED if data was successfully added to write buffer;
 *         OMCACHE_BUFFER_FULL if buffer was full and data was not written.
 */
int omcache_command(omcache_t *mc, omcache_req_t *req, omcache_resp_t *resp,
                    int server_index, int32_t timeout_msec);

// Server info

/**
 * Look up the server index of OMcache's internal server list for the server
 * that is used for actions regarding key.  The server index can be used with
 * omcache_server_info(), omcache_noop() and omcache_stat().
 * @param mc OMcache handle.
 * @param key The key for server index lookup.
 * @param key_len Length of key.
 * @return Server index.
 */
int omcache_server_index_for_key(omcache_t *mc, const unsigned char *key, size_t key_len);

typedef struct omcache_server_info_s
{
  char *hostname;
  int port;
} omcache_server_info_t;

/**
 * Retrieve information about the server at the given index.  All returned
 * information is stored locally in OMcache; this function will not perform
 * any I/O.
 * @param mc OMcache handle.
 * @param server_index Numeric index of the server from OMcache's internal
 *                     server list to look up.  Use
 *                     omcache_server_index_for_key() to get the indexes.
 * @return omcache_server_info_t structure.
 *         NULL if server_index is out of bounds.
 */
omcache_server_info_t *omcache_server_info(omcache_t *mc, int server_index);

/**
 * Free omcache_server_info_t returned by omcache_server_info()
 * @param mc OMcache handle.
 * @param info omcache_server_info_t structure as returned by
 *             omcache_server_info().
 * @return OMCACHE_OK on success.
 */
int omcache_server_info_free(omcache_t *mc, omcache_server_info_t *info);

// Commands

/**
 * Send a no-op to the given server.
 * @param mc OMcache handle.
 * @param server_index Numeric index of the server from OMcache's internal
 *                     server list to look up.  Use
 *                     omcache_server_index_for_key() to get the indexes.
 * @param timeout_msec Maximum number of milliseconds to block while waiting
 *                     for I/O to complete.  Zero means no blocking at all
 *                     and a negative value blocks indefinitely.
 * @return OMCACHE_OK if data was successfully written;
 *         OMCACHE_BUFFERED if data was successfully added to write buffer.
 *         OMCACHE_NO_SERVERS if server_index is out of bounds.
 */
int omcache_noop(omcache_t *mc,
                 int server_index, int32_t timeout_msec);

/**
 * Look up statistics for the given server.  This function can only be used
 * with a response callback, the callback will be called multiple times for
 * a single stat lookup request.
 * @param mc OMcache handle.
 * @param command Statistics type to look up or NULL for general statistics.
 *                Possible commands are backend specific, see memcached's
 *                documentation for the values it supports.
 * @param server_index Numeric index of the server from OMcache's internal
 *                     server list to look up.  Use
 *                     omcache_server_index_for_key() to get the indexes.
 * @param timeout_msec Maximum number of milliseconds to block while waiting
 *                     for I/O to complete.  Zero means no blocking at all
 *                     and a negative value blocks indefinitely.
 * @return OMCACHE_OK if data was successfully written;
 *         OMCACHE_BUFFERED if data was successfully added to write buffer.
 *         OMCACHE_NO_SERVERS if server_index is out of bounds.
 */
int omcache_stat(omcache_t *mc, const char *command,
                 int server_index, int32_t timeout_msec);

/**
 * Flush (delete) all entries from t
 * @param mc OMcache handle.
 * @param expiration Delete all entries older than this (0 for everything.)
 * @param server_index Numeric index of the server from OMcache's internal
 *                     server list to look up.  Use
 *                     omcache_server_index_for_key() to get the indexes.
 * @param timeout_msec Maximum number of milliseconds to block while waiting
 *                     for I/O to complete.  Zero means no blocking at all
 *                     and a negative value blocks indefinitely.
 * @return OMCACHE_OK if data was successfully written;
 *         OMCACHE_BUFFERED if data was successfully added to write buffer.
 *         OMCACHE_NO_SERVERS if server_index is out of bounds.
 */
int omcache_flush_all(omcache_t *mc, time_t expiration,
                      int server_index, int32_t timeout_msec);

/**
 * Set the given key to the given value.
 * @param mc OMcache handle.
 * @param key Key under which the value will be stored.
 * @param key_len Length of the key.
 * @param value New value to store in memcached.
 * @param value_len Length of the value.
 * @param expiration Expire the value after this time.
 * @param flags Flags to associate with the stored object.
 * @param cas CAS value for synchronization, if non-zero and an object with
 *            a different value already exists in memcached the request will
 *            fail with OMCACHE_KEY_EXISTS.
 * @param timeout_msec Maximum number of milliseconds to block while waiting
 *                     for I/O to complete.  Zero means no blocking at all
 *                     and a negative value blocks indefinitely.
 * @return OMCACHE_OK if data was successfully written;
 *         OMCACHE_BUFFERED if data was successfully added to write buffer.
 *         OMCACHE_KEY_EXISTS cas was set and did not match existing value.
 */
int omcache_set(omcache_t *mc,
                const unsigned char *key, size_t key_len,
                const unsigned char *value, size_t value_len,
                time_t expiration, uint32_t flags,
                uint64_t cas, int32_t timeout_msec);

/**
 * Add the given key with the given value if it does not yet exist in the
 * backend.
 * @param mc OMcache handle.
 * @param key Key under which the value will be stored.
 * @param key_len Length of the key.
 * @param value New value to store in memcached.
 * @param value_len Length of the value.
 * @param expiration Expire the value after this time.
 * @param flags Flags to associate with the stored object.
 * @param timeout_msec Maximum number of milliseconds to block while waiting
 *                     for I/O to complete.  Zero means no blocking at all
 *                     and a negative value blocks indefinitely.
 * @return OMCACHE_OK if data was successfully written;
 *         OMCACHE_BUFFERED if data was successfully added to write buffer.
 *         OMCACHE_KEY_EXISTS the key already exists in the backend.
 */
int omcache_add(omcache_t *mc,
                const unsigned char *key, size_t key_len,
                const unsigned char *value, size_t value_len,
                time_t expiration, uint32_t flags,
                int32_t timeout_msec);

/**
 * Replace the value associated with the given key if the key already exists
 * in the backend.
 * @param mc OMcache handle.
 * @param key Key under which the value will be stored.
 * @param key_len Length of the key.
 * @param value New value to store in memcached.
 * @param value_len Length of the value.
 * @param expiration Expire the value after this time.
 * @param flags Flags to associate with the stored object.
 * @param timeout_msec Maximum number of milliseconds to block while waiting
 *                     for I/O to complete.  Zero means no blocking at all
 *                     and a negative value blocks indefinitely.
 * @return OMCACHE_OK if data was successfully written;
 *         OMCACHE_BUFFERED if data was successfully added to write buffer.
 *         OMCACHE_NOT_FOUND the key was not set in the backend.
 */
int omcache_replace(omcache_t *mc,
                    const unsigned char *key, size_t key_len,
                    const unsigned char *value, size_t value_len,
                    time_t expiration, uint32_t flags,
                    int32_t timeout_msec);

/**
 * Atomically increment a counter for the given key.
 * @param mc OMcache handle.
 * @param key Key to access.
 * @param key_len Length of the key.
 * @param delta Increment the counter by this number.
 * @param initial Value to be set if the counter does not yet exist.
 * @param expiration Expire the value after this time.  If set to
 *                   OMCACHE_DELTA_NO_ADD the counter is not initialized in
 *                   case it does not yet exist in the backend.
 * @param timeout_msec Maximum number of milliseconds to block while waiting
 *                     for I/O to complete.  Zero means no blocking at all
 *                     and a negative value blocks indefinitely.
 * @return OMCACHE_OK if data was successfully written;
 *         OMCACHE_BUFFERED if data was successfully added to write buffer.
 *         OMCACHE_DELTA_BAD_VALUE the existing value in the backend is not
 *                                 a valid number.
 */
int omcache_increment(omcache_t *mc,
                      const unsigned char *key, size_t key_len,
                      uint64_t delta, uint64_t initial,
                      time_t expiration, uint64_t *value,
                      int32_t timeout_msec);

/**
 * Atomically decrement a counter for the given key.
 * @param mc OMcache handle.
 * @param key Key to access.
 * @param key_len Length of the key.
 * @param delta Decrement the counter by this number.
 * @param initial Value to be set if the counter does not yet exist.
 * @param expiration Expire the value after this time.  If set to
 *                   OMCACHE_DELTA_NO_ADD the counter is not initialized in
 *                   case it does not yet exist in the backend.
 * @param timeout_msec Maximum number of milliseconds to block while waiting
 *                     for I/O to complete.  Zero means no blocking at all
 *                     and a negative value blocks indefinitely.
 * @return OMCACHE_OK if data was successfully written;
 *         OMCACHE_BUFFERED if data was successfully added to write buffer.
 *         OMCACHE_DELTA_BAD_VALUE the existing value in the backend is not
 *                                 a valid number.
 */
int omcache_decrement(omcache_t *mc,
                      const unsigned char *key, size_t key_len,
                      uint64_t delta, uint64_t initial,
                      time_t expiration, uint64_t *value,
                      int32_t timeout_msec);

/**
 * Delete a key from the backend.
 * @param mc OMcache handle.
 * @param key Key to delete.
 * @param key_len Length of the key.
 * @param timeout_msec Maximum number of milliseconds to block while waiting
 *                     for I/O to complete.  Zero means no blocking at all
 *                     and a negative value blocks indefinitely.
 * @return OMCACHE_OK if data was successfully written;
 *         OMCACHE_BUFFERED if data was successfully added to write buffer.
 *         OMCACHE_NOT_FOUND the key was not set in the backend.
 */
int omcache_delete(omcache_t *mc,
                   const unsigned char *key, size_t key_len,
                   int32_t timeout_msec);

/**
 * Look up a key from the backend.
 * @param mc OMcache handle.
 * @param key Key to look up.
 * @param key_len Length of the key.
 * @param value Pointer to store the new value in, may be NULL.
 * @param value_len Pointer to store the length of the value in.
 * @param flags Pointer to store the retrieved object's flags in.
 * @param cas Pointer to store the retrieved object's CAS value in.
 * @param timeout_msec Maximum number of milliseconds to block while waiting
 *                     for I/O to complete.  Zero means no blocking at all
 *                     and a negative value blocks indefinitely.
 * @return OMCACHE_OK if data was successfully written;
 *         OMCACHE_NOT_FOUND the key was not set in the backend.
 *         OMCACHE_AGAIN value was not yet retrieved, call omcache_io()
 *                       to get it later.
 */
int omcache_get(omcache_t *mc,
                const unsigned char *key, size_t key_len,
                const unsigned char **value, size_t *value_len,
                uint32_t *flags, uint64_t *cas,
                int32_t timeout_msec);
