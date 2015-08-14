/*
 * omcache_libmemcached.h - a kludgy libmemcached API compatibility layer
 *
 * Copyright (c) 2013-2015, Oskari Saarenmaa <os@ohmu.fi>
 * All rights reserved.
 *
 * This file is under the Apache License, Version 2.0.
 * See the file `LICENSE` for details.
 *
 * NOTE: The functionality provided by this wrapper is limited and it is not
 * supported; it's provided to make it easier to prototype OMcache in simple
 * programs that use the libmemcached API.
 *
 */

#ifndef _OMCACHE_LIBMEMCACHED_H
#define OMCACHE_LIBMEMCACHED_H 1

#include "omcache.h"

#define MEMCACHED_EXPIRATION_NOT_ADD OMCACHE_DELTA_NO_ADD
#define MEMCACHED_SUCCESS OMCACHE_OK
#define MEMCACHED_FAILURE OMCACHE_FAIL
#define MEMCACHED_BUFFERED OMCACHE_BUFFERED
#define MEMCACHED_NOTFOUND OMCACHE_NOT_FOUND
#define MEMCACHED_END -1
#define MEMCACHED_SOME_ERRORS -1
#define LIBMEMCACHED_VERSION_HEX 0x01000003

// how long should we wait for commands to complete?
#ifndef MEMCACHED_COMMAND_TIMEOUT
#define MEMCACHED_COMMAND_TIMEOUT -1
#endif // !MEMCACHED_COMMAND_TIMEOUT

#ifndef MEMCACHED_READ_TIMEOUT
#define MEMCACHED_READ_TIMEOUT MEMCACHED_COMMAND_TIMEOUT
#endif // !MEMCACHED_READ_TIMEOUT

#ifndef MEMCACHED_WRITE_TIMEOUT
#define MEMCACHED_WRITE_TIMEOUT MEMCACHED_COMMAND_TIMEOUT
#endif // !MEMCACHED_WRITE_TIMEOUT

// memcached functions usually uses signed char *s, omcache uses unsigned char *s
#define omc_cc_to_cuc(v) (const unsigned char *) ({ const char *cc_ = (v); cc_; })
// we ignore some arguments and don't want to emit warnings about them
#define omc_unused_var(v) ({ __typeof__(v) __attribute__((unused)) uu__ = (v); })

typedef omcache_t memcached_st;
typedef char memcached_server_st;
typedef void * memcached_stat_st;
typedef int memcached_return;
typedef int memcached_return_t;
typedef omcache_server_info_t *memcached_server_instance_st;
typedef memcached_return_t (*memcached_server_fn)(memcached_st *, memcached_server_instance_st, void *);

typedef const char *memcached_behavior;
typedef const char *memcached_behavior_t;
typedef const char *memcached_hash;
typedef const char *memcached_hash_t;
typedef const char *memcached_server_distribution;
typedef const char *memcached_server_distribution_t;

#define memcached_create(mc) omcache_init()
#define memcached_free(mc) omcache_free(mc)
#define memcached_strerror(mc,rc) omcache_strerror(rc)
#define memcached_flush_buffers(mc) omcache_io((mc), NULL, NULL, NULL, NULL, -1)
#define memcached_flush(mc,expire) ({ \
    int srvidx_, rc_ = OMCACHE_OK; \
    for (srvidx_ = 0; rc_ == OMCACHE_OK; srvidx_ ++) \
        rc_ = omcache_flush_all((mc), (expire), srvidx_, -1) ; \
    (rc_ == OMCACHE_NO_SERVERS) ? OMCACHE_OK : rc_; })
#define memcached_increment(mc,key,key_len,offset,val) \
    omcache_increment((mc), omc_cc_to_cuc(key), (key_len), (offset), 0, OMCACHE_DELTA_NO_ADD, (val), MEMCACHED_WRITE_TIMEOUT)
#define memcached_increment_with_initial(mc,key,key_len,offset,initial,expire,val) \
    omcache_increment((mc), omc_cc_to_cuc(key), (key_len), (offset), (initial), (expire), (val), MEMCACHED_WRITE_TIMEOUT)
#define memcached_decrement(mc,key,key_len,offset,val) \
    omcache_decrement((mc), omc_cc_to_cuc(key), (key_len), (offset), 0, OMCACHE_DELTA_NO_ADD, (val), MEMCACHED_WRITE_TIMEOUT)
#define memcached_decrement_with_initial(mc,key,key_len,offset,initial,expire,val) \
    omcache_decrement((mc), omc_cc_to_cuc(key), (key_len), (offset), (initial), (expire), (val), MEMCACHED_WRITE_TIMEOUT)
#define memcached_add(mc,key,key_len,val,val_len,expire,flags) \
    omcache_add((mc), omc_cc_to_cuc(key), (key_len), omc_cc_to_cuc(val), (val_len), (expire), (flags), MEMCACHED_WRITE_TIMEOUT)
#define memcached_set(mc,key,key_len,val,val_len,expire,flags) \
    omcache_set((mc), omc_cc_to_cuc(key), (key_len), omc_cc_to_cuc(val), (val_len), (expire), (flags), 0, MEMCACHED_WRITE_TIMEOUT)
#define memcached_replace(mc,key,key_len,val,val_len,expire,flags) \
    omcache_replace((mc), omc_cc_to_cuc(key), (key_len), omc_cc_to_cuc(val), (val_len), (expire), (flags), MEMCACHED_WRITE_TIMEOUT)
#define memcached_touch(mc,key,key_len,expire) \
    omcache_touch((mc), omc_cc_to_cuc(key), (key_len), (expire))
// NOTE: memcached protocol doesn't have expiration for delete
#define memcached_delete(mc,key,key_len,expire) \
    ({ omc_unused_var(expire); omcache_delete((mc), omc_cc_to_cuc(key), (key_len), MEMCACHED_WRITE_TIMEOUT); })
// NOTE: memcached protocol doesn't have expiration or flags for append and prepend
#define memcached_append(mc,key,key_len,val,val_len,expire,flags) \
    ({  omc_unused_var(expire); omc_unused_var(flags); \
        omcache_append((mc), omc_cc_to_cuc(key), (key_len), omc_cc_to_cuc(val), (val_len), 0, MEMCACHED_WRITE_TIMEOUT); })
#define memcached_prepend(mc,key,key_len,val,val_len,expire,flags) \
    ({  omc_unused_var(expire); omc_unused_var(flags); \
        omcache_prepend((mc), omc_cc_to_cuc(key), (key_len), omc_cc_to_cuc(val), (val_len), 0, MEMCACHED_WRITE_TIMEOUT); })

#define memcached_get(mc,key,key_len,r_len,flags,rc) \
    ({  const unsigned char *val_; \
        *rc = omcache_get((mc), omc_cc_to_cuc(key), (key_len), &val_, (r_len), (flags), NULL, MEMCACHED_READ_TIMEOUT); \
        memcpy(malloc(*(r_len)), val_, *(r_len)); })

#define memcached_servers_parse(s) strdup(s)
#define memcached_server_push omcache_set_servers
#define memcached_server_list_free(s) free(s)

#define memcached_server_name(s) (s)->hostname
#define memcached_server_port(s) (s)->port
#define memcached_server_cursor(mc,cb_list,cb_ctx,cb_cnt) \
    ({  int cbidx_, srvidx_ = 0, res_ = MEMCACHED_SUCCESS; \
        omcache_server_info_t *srvnfo_ = NULL; \
        while ((res_ == MEMCACHED_SUCCESS) && (srvnfo_ = omcache_server_info((mc), srvidx_))) { \
            for (cbidx_ = 0; (res_ == MEMCACHED_SUCCESS) && (cbidx_ < (cb_cnt)); cbidx_ ++) { \
                res_ = (cb_list)[cbidx_]((mc), srvnfo_, (cb_ctx)); \
            } srvidx_ ++; \
            omcache_server_info_free((mc), srvnfo_); } \
        res_; })

// omcache can't implement memcached_stat_servername which doesn't use memcached_st
// use omcache_stat_by_server_name() instead
#define memcached_stat_servername(stat,keys,hostname,port) \
    ({ *(stat) = NULL; MEMCACHED_SUCCESS; })
#define memcached_stat_get_keys(mc,stat,rc) \
    ({ *(rc) = MEMCACHED_SUCCESS; NULL; })
#define memcached_stat_get_value(mc,stat,key,rc) \
    ({ *(rc) = MEMCACHED_FAILURE; NULL; })

// various omcache_set_* apis need to be used instead of behaviors
#define memcached_behavior_set(m,k,v) MEMCACHED_FAILURE

// omcache_get_multi and omcache_io need to be called instead of these
#define memcached_mget(mc,keys,key_lens,arr_len) MEMCACHED_FAILURE
#define memcached_fetch(mc,key,key_len,val_len,flags,rc) ({ *rc = MEMCACHED_FAILURE; NULL; })

#endif // !_OMCACHE_LIBMEMCACHED_H
