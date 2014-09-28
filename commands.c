/*
 * Commands for OMcache
 *
 * Copyright (c) 2013-2014, Oskari Saarenmaa <os@ohmu.fi>
 * All rights reserved.
 *
 * This file is under the 2-clause BSD license.
 * See the file `LICENSE` for details.
 *
 */

#include <endian.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "memcached_protocol_binary.h"
#include "omcache.h"

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

static int omc_set_cmd(omcache_t *mc, protocol_binary_command opcode,
                       const unsigned char *key, size_t key_len,
                       const unsigned char *value, size_t value_len,
                       time_t expiration, uint32_t flags)
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

int omcache_set(omcache_t *mc,
                const unsigned char *key, size_t key_len,
                const unsigned char *value, size_t value_len,
                time_t expiration, uint32_t flags)
{
  return omc_set_cmd(mc, PROTOCOL_BINARY_CMD_SET,
                     key, key_len, value, value_len,
                     expiration, flags);
}

int omcache_add(omcache_t *mc,
                const unsigned char *key, size_t key_len,
                const unsigned char *value, size_t value_len,
                time_t expiration, uint32_t flags)
{
  return omc_set_cmd(mc, PROTOCOL_BINARY_CMD_ADD,
                     key, key_len, value, value_len,
                     expiration, flags);
}

int omcache_replace(omcache_t *mc,
                    const unsigned char *key, size_t key_len,
                    const unsigned char *value, size_t value_len,
                    time_t expiration, uint32_t flags)
{
  return omc_set_cmd(mc, PROTOCOL_BINARY_CMD_REPLACE,
                     key, key_len, value, value_len,
                     expiration, flags);
}

static int omc_ctr_cmd(omcache_t *mc, protocol_binary_command opcode,
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
  return omc_ctr_cmd(mc, PROTOCOL_BINARY_CMD_INCREMENT,
                     key, key_len, delta, initial, expiration);
}

int omcache_decrement(omcache_t *mc,
                      const unsigned char *key, size_t key_len,
                      uint64_t delta, uint64_t initial,
                      time_t expiration)
{
  return omc_ctr_cmd(mc, PROTOCOL_BINARY_CMD_DECREMENT,
                     key, key_len, delta, initial, expiration);
}

int omcache_delete(omcache_t *mc,
                   const unsigned char *key, size_t key_len)
{
  protocol_binary_request_delete req = {.bytes = {0}};
  req.message.header.request.opcode = PROTOCOL_BINARY_CMD_DELETE;
  req.message.header.request.keylen = htobe16((uint16_t) key_len);
  req.message.header.request.bodylen = htobe32((uint32_t) key_len);
  omcache_req_t oreq = {.header = req.bytes, .key = key, .data = NULL};
  return omcache_write(mc, &oreq);
}

int omcache_get(omcache_t *mc,
                const unsigned char *key, size_t key_len,
                const unsigned char **value, size_t *value_len,
                uint32_t *flags)
{
  protocol_binary_request_getk req = {.bytes = {0}};
  req.message.header.request.opcode = PROTOCOL_BINARY_CMD_GETK;
  req.message.header.request.keylen = htobe16((uint16_t) key_len);
  req.message.header.request.bodylen = htobe32(key_len);
  omcache_req_t oreq = {.header = req.bytes, .key = key, .data = NULL};
  omcache_resp_t oresp;
  int ret = omcache_command(mc, &oreq, value ? &oresp : NULL, -1);
  if (value == NULL)
    return ret;
  if (ret == OMCACHE_OK)
    {
      protocol_binary_response_getk *resp = (protocol_binary_response_getk *) oresp.header;
      *value = oresp.data;
      if (value_len != NULL)
        *value_len = be32toh(resp->message.header.response.bodylen) -
            be16toh(resp->message.header.response.keylen) -
            resp->message.header.response.extlen;
      if (flags != NULL)
        *flags = be32toh(resp->message.body.flags);
    }
  else
    {
      *value = NULL;
      if (value_len != NULL)
        *value_len = 0;
      if (flags != NULL)
        *flags = 0;
    }
  return ret;
}
