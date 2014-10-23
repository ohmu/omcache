/*
 * omcache_libmemcached.h - a kludgy libmemcached API compatibility layer
 *
 * Written by Oskari Saarenmaa <os@ohmu.fi>, and is placed in the public
 * domain.  The author hereby disclaims copyright to this source code.
 *
 */

#ifndef _OMCACHE_LIBMEMCACHED_H
#define OMCACHE_LIBMEMCACHED_H 1

#include "omcache.h"

#define MEMCACHED_EXPIRATION_NOT_ADD 0xffffffffU
#define MEMCACHED_SUCCESS OMCACHE_OK
#define MEMCACHED_FAILURE OMCACHE_FAIL
#define MEMCACHED_BUFFERED OMCACHE_BUFFERED
#define MEMCACHED_NOTFOUND OMCACHE_NOT_FOUND
#define MEMCACHED_END -1
#define MEMCACHED_SOME_ERRORS -1
#define LIBMEMCACHED_VERSION_HEX 0x01000003

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
#define memcached_increment_with_initial(mc,key,key_len,offset,initial,expiration,val) \
    omcache_increment((mc), omc_cc_to_cuc(key), (key_len), (offset), (initial), (expiration), (val), 0)
#define memcached_decrement_with_initial(mc,key,key_len,offset,initial,expiration,val) \
    omcache_decrement((mc), omc_cc_to_cuc(key), (key_len), (offset), (initial), (expiration), (val), 0)
#define memcached_delete(mc,key,key_len,hold) \
    ({ omc_unused_var(hold); omcache_delete((mc), omc_cc_to_cuc(key), (key_len), 0); })
#define memcached_add(mc,key,key_len,val,val_len,exp,flags) \
    omcache_add((mc), omc_cc_to_cuc(key), (key_len), omc_cc_to_cuc(val), (val_len), (exp), (flags), 0)
#define memcached_set(mc,key,key_len,val,val_len,exp,flags) \
    omcache_set((mc), omc_cc_to_cuc(key), (key_len), omc_cc_to_cuc(val), (val_len), (exp), (flags), 0, 0)
#define memcached_replace(mc,key,key_len,val,val_len,exp,flags) \
    omcache_replace((mc), omc_cc_to_cuc(key), (key_len), omc_cc_to_cuc(val), (val_len), (exp), (flags), 0)
#define memcached_get(mc,key,key_len,r_len,flags,rc) \
    ({  const unsigned char *val_; \
        *rc = omcache_get((mc), omc_cc_to_cuc(key), (key_len), &val_, (r_len), (flags), NULL, -1); \
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

#define memcached_behavior_set(m,k,v) MEMCACHED_FAILURE
#define memcached_mget(mc,keys,key_lens,arr_len) MEMCACHED_FAILURE
#define memcached_fetch(mc,key,key_len,val_len,flags,rc) ({ *rc = MEMCACHED_FAILURE; NULL; })
#define memcached_append(mc,key,key_len,val,val_len,expire,flags) MEMCACHED_FAILURE
#define memcached_prepend(mc,key,key_len,val,val_len,expire,flags) MEMCACHED_FAILURE

#endif // !_OMCACHE_LIBMEMCACHED_H
