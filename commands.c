/*
 * Commands for OMcache
 *
 * Copyright (c) 2013-2014, Oskari Saarenmaa <os@ohmu.fi>
 * All rights reserved.
 *
 * This file is under the Apache License, Version 2.0.
 * See the file `LICENSE` for details.
 *
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "compat.h"
// Note: commands.c only uses the "public" omcache api
#include "omcache.h"
#include "memcached_protocol_binary.h"

#define QCMD(k) (timeout_msec ? k : k ## Q)


int omcache_noop(omcache_t *mc, int server_index, int32_t timeout_msec)
{
  omcache_req_t req[1] = {{
    .server_index = server_index,
    .header = {
      .opcode = PROTOCOL_BINARY_CMD_NOOP,
      },
    }};
  return omcache_command_status(mc, req, timeout_msec);
}

int omcache_stat(omcache_t *mc, const char *command,
                 omcache_value_t *values, size_t *value_count,
                 int server_index, int32_t timeout_msec)
{
  size_t key_len = command ? strlen(command) : 0;
  size_t req_count = 1;
  omcache_req_t req[1] = {{
    .server_index = server_index,
    .header = {
      .opcode = PROTOCOL_BINARY_CMD_STAT,
      .keylen = htobe16(key_len),
      .bodylen = htobe32(key_len),
      },
    .key = (const unsigned char *) command,
    }};
  return omcache_command(mc, req, &req_count, values, value_count, timeout_msec);
}

int omcache_flush_all(omcache_t *mc, time_t expiration, int server_index, int32_t timeout_msec)
{
  uint32_t body_exp = htobe32(expiration);
  omcache_req_t req[1] = {{
    .server_index = server_index,
    .header = {
      .opcode = PROTOCOL_BINARY_CMD_FLUSH,
      .extlen = sizeof(body_exp),
      .bodylen = htobe32(sizeof(body_exp)),
      },
    .extra = &body_exp,
    }};
  return omcache_command_status(mc, req, timeout_msec);
}

struct protocol_binary_set_request_body_s {
  uint32_t flags;
  uint32_t expiration;
} __attribute__((packed));

static int omc_set_cmd(omcache_t *mc, protocol_binary_command opcode,
                       const unsigned char *key, size_t key_len,
                       const unsigned char *value, size_t value_len,
                       time_t expiration, uint32_t flags,
                       uint64_t cas, int32_t timeout_msec)
{
  struct protocol_binary_set_request_body_s extra = {
    .flags = htobe32(flags),
    .expiration = htobe32(expiration),
    };
  size_t extra_len = sizeof(extra);
  if (opcode == PROTOCOL_BINARY_CMD_APPEND || opcode == PROTOCOL_BINARY_CMD_APPENDQ ||
      opcode == PROTOCOL_BINARY_CMD_PREPEND || opcode == PROTOCOL_BINARY_CMD_PREPENDQ)
    extra_len = 0;
  omcache_req_t req[1] = {{
    .server_index = -1,
    .header = {
      .opcode = opcode,
      .extlen = extra_len,
      .keylen = htobe16(key_len),
      .bodylen = htobe32(key_len + value_len + extra_len),
      .cas = htobe64(cas),
      },
    .extra = &extra,
    .key = key,
    .data = value,
    }};
  return omcache_command_status(mc, req, timeout_msec);
}

int omcache_set(omcache_t *mc,
                const unsigned char *key, size_t key_len,
                const unsigned char *value, size_t value_len,
                time_t expiration, uint32_t flags,
                uint64_t cas, int32_t timeout_msec)
{
  return omc_set_cmd(mc, QCMD(PROTOCOL_BINARY_CMD_SET),
                     key, key_len, value, value_len,
                     expiration, flags, cas, timeout_msec);
}

int omcache_add(omcache_t *mc,
                const unsigned char *key, size_t key_len,
                const unsigned char *value, size_t value_len,
                time_t expiration, uint32_t flags,
                int32_t timeout_msec)
{
  return omc_set_cmd(mc, QCMD(PROTOCOL_BINARY_CMD_ADD),
                     key, key_len, value, value_len,
                     expiration, flags, 0, timeout_msec);
}

int omcache_replace(omcache_t *mc,
                    const unsigned char *key, size_t key_len,
                    const unsigned char *value, size_t value_len,
                    time_t expiration, uint32_t flags,
                    int32_t timeout_msec)
{
  return omc_set_cmd(mc, QCMD(PROTOCOL_BINARY_CMD_REPLACE),
                     key, key_len, value, value_len,
                     expiration, flags, 0, timeout_msec);
}

int omcache_append(omcache_t *mc,
                   const unsigned char *key, size_t key_len,
                   const unsigned char *value, size_t value_len,
                   uint64_t cas, int32_t timeout_msec)
{
  return omc_set_cmd(mc, QCMD(PROTOCOL_BINARY_CMD_APPEND),
                     key, key_len, value, value_len,
                     0, 0, cas, timeout_msec);
}

int omcache_prepend(omcache_t *mc,
                   const unsigned char *key, size_t key_len,
                   const unsigned char *value, size_t value_len,
                   uint64_t cas, int32_t timeout_msec)
{
  return omc_set_cmd(mc, QCMD(PROTOCOL_BINARY_CMD_PREPEND),
                     key, key_len, value, value_len,
                     0, 0, cas, timeout_msec);
}

struct protocol_binary_delta_request_body_s {
  uint64_t delta;
  uint64_t initial;
  uint32_t expiration;
} __attribute__((packed));

static int omc_ctr_cmd(omcache_t *mc, protocol_binary_command opcode,
                       const unsigned char *key, size_t key_len,
                       uint64_t delta, uint64_t initial,
                       time_t expiration, uint64_t *valuep,
                       int32_t timeout_msec)
{
  struct protocol_binary_delta_request_body_s body = {
    .delta = htobe64(delta),
    .initial = htobe64(initial),
    .expiration = htobe32(expiration),
    };
  omcache_req_t req[1] = {{
    .server_index = -1,
    .header = {
      .opcode = opcode,
      .extlen = sizeof(body),
      .keylen = htobe16(key_len),
      .bodylen = htobe32(key_len + sizeof(body)),
      },
    .extra = &body,
    .key = key,
    }};
  omcache_value_t value = {0};
  size_t req_count = 1, value_count = 1;
  int ret = omcache_command(mc, req, &req_count, &value, &value_count, timeout_msec);
  if (ret == OMCACHE_OK && value_count)
    ret = value.status;
  if (valuep)
    *valuep = (ret == OMCACHE_OK) ? value.delta_value : 0;
  return ret;
}

int omcache_increment(omcache_t *mc,
                      const unsigned char *key, size_t key_len,
                      uint64_t delta, uint64_t initial,
                      time_t expiration, uint64_t *value,
                      int32_t timeout_msec)
{
  return omc_ctr_cmd(mc, QCMD(PROTOCOL_BINARY_CMD_INCREMENT),
                     key, key_len, delta, initial, expiration,
                     value, timeout_msec);
}

int omcache_decrement(omcache_t *mc,
                      const unsigned char *key, size_t key_len,
                      uint64_t delta, uint64_t initial,
                      time_t expiration, uint64_t *value,
                      int32_t timeout_msec)
{
  return omc_ctr_cmd(mc, QCMD(PROTOCOL_BINARY_CMD_DECREMENT),
                     key, key_len, delta, initial, expiration,
                     value, timeout_msec);
}

int omcache_delete(omcache_t *mc,
                   const unsigned char *key, size_t key_len,
                   int32_t timeout_msec)
{
  omcache_req_t req[1] = {{
    .server_index = -1,
    .header = {
      .opcode = QCMD(PROTOCOL_BINARY_CMD_DELETE),
      .keylen = htobe16(key_len),
      .bodylen = htobe32(key_len),
      },
    .key = key,
    }};
  return omcache_command_status(mc, req, timeout_msec);
}

int omcache_touch(omcache_t *mc,
                  const unsigned char *key, size_t key_len,
                  time_t expiration, int32_t timeout_msec)
{
  uint32_t body_exp = htobe32(expiration);
  omcache_req_t req[1] = {{
    .server_index = -1,
    .header = {
      .opcode = PROTOCOL_BINARY_CMD_TOUCH,
      .extlen = sizeof(body_exp),
      .keylen = htobe16(key_len),
      .bodylen = htobe32(key_len + sizeof(body_exp)),
      },
    .extra = &body_exp,
    .key = key,
    }};
  return omcache_command_status(mc, req, timeout_msec);
}

static int omc_get_multi_cmd(omcache_t *mc,
                             protocol_binary_command opcode,
                             const unsigned char **keys,
                             size_t *key_lens,
                             uint32_t *be_expirations,
                             size_t key_count,
                             omcache_req_t *requests,
                             size_t *req_count,
                             omcache_value_t *values,
                             size_t *value_count,
                             int32_t timeout_msec)
{
  if (req_count == NULL || *req_count < key_count)
    return OMCACHE_INVALID;

  memset(requests, 0, sizeof(*requests) * *req_count);
  if (values && value_count)
    memset(values, 0, sizeof(*values) * *value_count);

  for (size_t i = 0; i < key_count; i ++)
    {
      requests[i].server_index = -1;
      requests[i].header.opcode = opcode;
      requests[i].header.keylen = htobe16(key_lens[i]);
      if (opcode == PROTOCOL_BINARY_CMD_GAT || opcode == PROTOCOL_BINARY_CMD_GATQ ||
          opcode == PROTOCOL_BINARY_CMD_GATK || opcode == PROTOCOL_BINARY_CMD_GATKQ)
        {
          requests[i].header.extlen = sizeof(uint32_t);
          requests[i].extra = &be_expirations[i];
        }
      requests[i].header.bodylen = htobe32(key_lens[i] + requests[i].header.extlen);
      requests[i].key = keys[i];
    }
  return omcache_command(mc, requests, req_count, values, value_count, timeout_msec);
}

int omcache_get_multi(omcache_t *mc,
                      const unsigned char **keys,
                      size_t *key_lens,
                      size_t key_count,
                      omcache_req_t *requests,
                      size_t *req_count,
                      omcache_value_t *values,
                      size_t *value_count,
                      int32_t timeout_msec)
{
  return omc_get_multi_cmd(mc, PROTOCOL_BINARY_CMD_GETKQ,
                           keys, key_lens, NULL, key_count,
                           requests, req_count, values, value_count,
                           timeout_msec);
}

int omcache_gat_multi(omcache_t *mc,
                      const unsigned char **keys,
                      size_t *key_lens,
                      time_t *expirations,
                      size_t key_count,
                      omcache_req_t *requests,
                      size_t *req_count,
                      omcache_value_t *values,
                      size_t *value_count,
                      int32_t timeout_msec)
{
  uint32_t be_expirations[key_count];
  for (size_t i = 0; i < key_count; i ++)
   be_expirations[i] = htobe32(expirations[i]);
  return omc_get_multi_cmd(mc, PROTOCOL_BINARY_CMD_GATKQ,
                           keys, key_lens, be_expirations, key_count,
                           requests, req_count, values, value_count,
                           timeout_msec);
}

static int omc_get_cmd(omcache_t *mc, protocol_binary_command opcode,
                       const unsigned char *key, size_t key_len,
                       const unsigned char **valuep, size_t *value_len,
                       uint32_t be_expiration, uint32_t *flags, uint64_t *cas,
                       int32_t timeout_msec)
{
  omcache_req_t req;
  omcache_value_t value = {0};
  size_t req_count = 1, value_count = 1;
  int ret = omc_get_multi_cmd(mc, opcode, &key, &key_len, &be_expiration, 1, &req, &req_count,
                              &value, &value_count, timeout_msec);
  if (value_count)
    ret = value.status;
  if (value_count == 0 && ret == OMCACHE_OK)
    ret = OMCACHE_NOT_FOUND;
  else if (ret == OMCACHE_AGAIN && valuep == NULL)
    ret = OMCACHE_OK;
  if (valuep)
    *valuep = value.data;
  if (value_len)
    *value_len = value.data_len;
  if (flags)
    *flags = value.flags;
  if (cas)
    *cas = value.cas;
  return ret;
}

int omcache_get(omcache_t *mc,
                const unsigned char *key, size_t key_len,
                const unsigned char **value, size_t *value_len,
                uint32_t *flags, uint64_t *cas,
                int32_t timeout_msec)
{
  return omc_get_cmd(mc, QCMD(PROTOCOL_BINARY_CMD_GETK), key, key_len,
                     value, value_len, 0, flags, cas, timeout_msec);
}

int omcache_gat(omcache_t *mc,
                const unsigned char *key, size_t key_len,
                const unsigned char **value, size_t *value_len,
                time_t expiration,
                uint32_t *flags, uint64_t *cas,
                int32_t timeout_msec)
{
  return omc_get_cmd(mc, QCMD(PROTOCOL_BINARY_CMD_GATK), key, key_len,
                     value, value_len, htobe32(expiration),
                     flags, cas, timeout_msec);
}
