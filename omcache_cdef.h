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
  OMCACHE_OK = 0x00,               ///< Success
  OMCACHE_NOT_FOUND = 0x01,        ///< Key not found from memcached
  OMCACHE_KEY_EXISTS = 0x02,       ///< Conflicting key exists in memcached
  OMCACHE_TOO_LARGE_VALUE = 0x03,  ///< Value size exceeds maximum
  OMCACHE_DELTA_BAD_VALUE = 0x06,  ///< Existing value can not be
                                   ///  incremented or decremented
  OMCACHE_FAIL = 0x0FFF,           ///< Command failed in memcached
  OMCACHE_AGAIN = 0x1001,          ///< Call would block, try again
  OMCACHE_INVALID,                 ///< Invalid parameters
  OMCACHE_BUFFERED,                ///< Data buffered in OMcache
  OMCACHE_BUFFER_FULL,             ///< Buffer full, command dropped
  OMCACHE_NO_SERVERS,              ///< No server available
  OMCACHE_SERVER_FAILURE,          ///< Failure communicating to server
} omcache_ret_t;

typedef struct omcache_s omcache_t;

typedef struct omcache_req_s {
    int server_index;           ///< Opaque integer identifying the server to
                                ///  use when the request type does not use
                                ///  a key (NOOP, VERSION and STATS).
                                ///  -1 when server is selected by key.
    struct omcache_req_header_s {
        uint8_t magic;          ///< Always PROTOCOL_BINARY_REQ (0x80),
                                ///  set by OMcache
        uint8_t opcode;         ///< Command type
        uint16_t keylen;        ///< Length of key, in network byte order
        uint8_t extlen;         ///< Length of structured extra data
        uint8_t datatype;       ///< Always PROTOCOL_BINARY_RAW_BYTES (0x00),
                                ///  set by OMcache
        uint16_t reserved;      ///< Reserved, do not set
        uint32_t bodylen;       ///< Request body length, in network byte
                                ///  order, includes extra, key and data
        uint32_t opaque;        ///< Request identifier, set by OMcache
        uint64_t cas;           ///< CAS value for synchronization
    } header;                   ///< Memcache binary protocol header struct
    void *extra;                ///< Extra structured data sent for some
                                ///  request types
    const unsigned char *key;   ///< Object key
    const unsigned char *data;  ///< Object value
} omcache_req_t;

typedef struct omcache_value_s {
    int status;                 ///< Response status (omcache_ret_t)
    const unsigned char *key;   ///< Response key (if any)
    size_t key_len;             ///< Response key length
    const unsigned char *data;  ///< Response data (if any)
    size_t data_len;            ///< Response data length
    uint32_t flags;             ///< Flags associated with the object
    uint64_t cas;               ///< CAS value for synchronization
    uint64_t delta_value;       ///< Value returned in delta operations
} omcache_value_t;

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
 * @param context Const char * log message prefix.
 * @param level Log message level.
 * @param msg The actual log message.
 */
void omcache_log_stderr(void *context, int level, const char *msg);

/**
 * Set a log callback for the OMcache handle.
 * @param mc OMcache handle.
 * @param level Maximum event level for logging, 0 for everything but debug.
 * @param func Callback function to call for each generated log message.
 * @param resp_cb_context Opaque context to pass to the callback function.
 * @return OMCACHE_OK on success.
 */
int omcache_set_log_callback(omcache_t *mc, int level, omcache_log_callback_func *func, void *context);

/**
 * Response callback type.
 * @param mc OMcache handle.
 * @param result Response value object.
 * @param context Opaque context set in omcache_set_response_callback().
 */
typedef void (omcache_response_callback_func)(omcache_t *mc, omcache_value_t *result, void *context);

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
 * @param reqs Array of requests for which we want responses.
 * @param req_count Number of requests in reqs array.  Will be zeroed once
 *                  all requests have been handled.
 * @param values Pointer to an array of omcache_value_t structures to store
 *               the responses found.  If there aren't enough value structs
 *               to store all requests found the responses will be silently
 *               dropped.  The memory pointed to by pointers in the
 *               omcache_value_t structs will be overwritten on subsequent
 *               calls to omcache_io and must not be freed by the caller.
 *               Note that all request types may not generate a response,
 *               namely a GETQ or GETKQ request for a non-existent key will
 *               not cause any response to be sent by the server or stored
 *               by OMcache even if omcache_io return status is OMCACHE_OK.
 * @param value_count Number of response structures in values array.  Will
 *                    be modified to contain the number of stored responses.
 * @param timeout_msec Maximum number of milliseconds to block while waiting
 *                     for I/O to complete.  Zero means no blocking at all
 *                     and a negative value blocks indefinitely until at
 *                     least one message has been received.
 * @return OMCACHE_OK If all responses were received;
 *         OMCACHE_AGAIN If there is more data to read.
*/
int omcache_io(omcache_t *mc,
               omcache_req_t *reqs, size_t *req_count,
               omcache_value_t *values, size_t *value_count,
               int32_t timeout_msec);

/**
 * Send a request to memcache and read the response status.
 * @param mc OMcache handle.
 * @param req An omcache_req_t struct containing at the request header
 *            and optionally a key and data.
 * @param timeout_msec Maximum number of milliseconds to block while waiting
 *                     for the response.  Zero means no blocking at all and
 *                     a negative value blocks indefinitely until at a
 *                     response is received or an error occurs.
 * @return OMCACHE_OK if data was successfully written;
 *         OMCACHE_BUFFERED if data was successfully added to write buffer;
 *         OMCACHE_BUFFER_FULL if buffer was full and data was not written;
 *         OMCACHE_NOT_FOUND
 *         OMCACHE_KEY_EXISTS
 *         OMCACHE_DELTA_BAD_VALUE
 */
int omcache_command_status(omcache_t *mc, omcache_req_t *req, int32_t timeout_msec);

/**
 * Send multiple requests to memcache and start reading their responses.
 * @param mc OMcache handle.
 * @param reqs Array of requests to send, handled like in omcache_io().
 * @param req_count reqs length, handled like in omcache_io().
 * @param values Array to store responses in, handled like in omcache_io().
 * @param value_count values length, handled like in omcache_io().
 * @param timeout_msec Maximum number of milliseconds to block while waiting
 *                     for the response.  Zero means no blocking at all and
 *                     a negative value blocks indefinitely until at a
 *                     response is received or an error occurs.
 * @return OMCACHE_OK if data was successfully written;
 *         OMCACHE_BUFFERED if data was successfully added to write buffer;
 *         OMCACHE_BUFFER_FULL if buffer was full and data was not written.
 */
int omcache_command(omcache_t *mc,
                    omcache_req_t *reqs, size_t *req_countp,
                    omcache_value_t *values, size_t *value_count,
                    int32_t timeout_msec);

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
  // Since OMcache 0.1.0: make sure to verify omcache_version in returned
  // struct matches the header version being used in the application
  int omcache_version;  ///< OMcache client version
  int server_index;     ///< Server index
  char *hostname;       ///< Hostname of the server
  int port;             ///< Port number of the server
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
 * @param values Array of omcache_value_t structures to be will be filled
 *               with the results of the stats lookup.
 * @param value_count Number of omcache_value_t structures in values.
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
                 omcache_value_t *values, size_t *value_count,
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
 * @param value Counter value after the increment operation.
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
 * @param value Counter value after the decrement operation.
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
 * Look up a key from a backend.
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

/**
 * Look up multiple keys from the backends.
 * @param mc OMcache handle.
 * @param keys Array of pointers to keys to look up.
 * @param key_lens Array of lengths of keys.
 * @param key_count Number of pointers in keys array.
 * @param reqs Array of request structures to store pending requests in.
 *             If this function can't complete all lookups in a single round
 *             of I/O operations the pending requests are stored in this
 *             array which can be passed to omcache_io() to complete the
 *             remaining requests.
 * @param req_count Number of requests in reqs array.  Will be zeroed once
 *                  all requests have been handled.
 * @param values Array to store responses in, handled like in omcache_io().
 * @param value_count values length, handled like in omcache_io().
 * @param timeout_msec Maximum number of milliseconds to block while waiting
 *                     for I/O to complete.  Zero means no blocking at all
 *                     and a negative value blocks indefinitely.
 * @return OMCACHE_OK All requests were handled;
 *         OMCACHE_AGAIN Not all values were retrieved,
 *                       call omcache_io() to retrieve them.
 */
int omcache_get_multi(omcache_t *mc,
                      const unsigned char **keys,
                      size_t *key_lens,
                      size_t key_count,
                      omcache_req_t *reqs,
                      size_t *req_count,
                      omcache_value_t *values,
                      size_t *value_count,
                      int32_t timeout_msec);
