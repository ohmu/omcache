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

// memcached functions usually uses signed char *s, omcache uses unsigned char *s
#define cc_to_cuc(v) (const unsigned char *) ({ const char *cc_ = (v); cc_; })

typedef omcache_t memcached_st;
typedef char memcached_server_st;
typedef void * memcached_server_instance_st;
typedef void * memcached_stat_st;
typedef int memcached_return;
typedef int memcached_return_t;
typedef memcached_return_t (*memcached_server_fn)(memcached_st *, memcached_server_instance_st, void *);

#define memcached_create(mc) omcache_init()
#define memcached_free(mc) omcache_free(mc)
#define memcached_strerror omcache_strerror
#define memcached_flush_buffers(mc) omcache_io((mc), -1, 0, NULL)
#define memcached_increment_with_initial(mc,key,key_len,offset,initial,expiration,val) \
    ({ *val = 0; omcache_increment((mc), cc_to_cuc(key), (key_len), (offset), (initial), (expiration)); })
#define memcached_decrement_with_initial(mc,key,key_len,offset,initial,expiration,val) \
    ({ *val = 0; omcache_decrement((mc), cc_to_cuc(key), (key_len), (offset), (initial), (expiration)); })
#define memcached_delete(mc,key,key_len,hold) \
    omcache_delete((mc), cc_to_cuc(key), (key_len))
#define memcached_add(mc,key,key_len,val,val_len,exp,flags) \
    omcache_add((mc), cc_to_cuc(key), (key_len), cc_to_cuc(val), (val_len), (exp), (flags))
#define memcached_set(mc,key,key_len,val,val_len,exp,flags) \
    omcache_set((mc), cc_to_cuc(key), (key_len), cc_to_cuc(val), (val_len), (exp), (flags))
#define memcached_replace(mc,key,key_len,val,val_len,exp,flags) \
    omcache_replace((mc), cc_to_cuc(key), (key_len), cc_to_cuc(val), (val_len), (exp), (flags))
#define memcached_get(mc,key,key_len,r_len,flags,rc) \
    ({  const unsigned char *val_; \
        *rc = omcache_get((mc), cc_to_cuc(key), (key_len), &val_, (r_len), (flags)); \
        memcpy(malloc(*(r_len)), val_, *(r_len)); })

#define memcached_servers_parse(s) strdup(s)
#define memcached_server_push omcache_set_servers
#define memcached_server_list_free(s) free(s)

#define memcached_behavior_set(m,k,v) MEMCACHED_FAILURE
#define memcached_flush(mc,expire) MEMCACHED_FAILURE
#define memcached_mget(mc,keys,key_lens,arr_len) MEMCACHED_FAILURE
#define memcached_fetch(mc,key,key_len,val_len,flags,rc) ({ *rc = MEMCACHED_FAILURE; NULL; })
#define memcached_append(mc,key,key_len,val,val_len,expire,flags) MEMCACHED_FAILURE
#define memcached_prepend(mc,key,key_len,val,val_len,expire,flags) MEMCACHED_FAILURE
#define memcached_stat_servername(a,b,c,d) MEMCACHED_FAILURE
#define memcached_stat_get_keys(a,b,rc) ({ *rc = MEMCACHED_FAILURE; NULL; })
#define memcached_stat_get_value(a,b,c,rc) ({ *rc = MEMCACHED_FAILURE; NULL; })
#define memcached_server_name(s) "null"
#define memcached_server_port(s) 0
#define memcached_server_cursor(mc,a,b,c) MEMCACHED_FAILURE

#endif // !_OMCACHE_LIBMEMCACHED_H
