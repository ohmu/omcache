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

typedef omcache_t memcached_st;
typedef const char memcached_server_st;
typedef void * memcached_server_instance_st;
typedef void * memcached_stat_st;
typedef int memcached_return;
typedef int memcached_return_t;
typedef memcached_return_t (*memcached_server_fn)(memcached_st *, memcached_server_instance_st, void *);

#define memcached_create(mc) omcache_init()
#define memcached_strerror omcache_strerror
#define memcached_flush_buffers(mc) omcache_flush_buffers((mc), -1)
#define memcached_increment_with_initial(mc,key,key_len,offset,initial,expiration,val) \
    ({ *val = 0; omcache_increment((mc),(key),(key_len),(offset),(initial),(expiration)); })
#define memcached_decrement_with_initial(mc,key,key_len,offset,initial,expiration,val) \
    ({ *val = 0; omcache_decrement((mc),(key),(key_len),(offset),(initial),(expiration)); })
#define memcached_delete(mc,key,key_len,hold) omcache_delete((mc),(key),(key_len))
#define memcached_add omcache_add
#define memcached_set omcache_set
#define memcached_replace omcache_replace

#define memcached_servers_parse(s) (s)
#define memcached_server_push omcache_set_servers
#define memcached_server_list_free(s) MEMCACHED_SUCCESS

#define memcached_behavior_set(m,k,v) MEMCACHED_FAILURE
#define memcached_flush(mc,expire) MEMCACHED_FAILURE
#define memcached_get(mc,key,key_len,r_len,flags,rc) ({ *rc = MEMCACHED_FAILURE; NULL; })
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
