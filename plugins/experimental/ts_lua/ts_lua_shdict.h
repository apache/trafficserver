#ifndef _TS_HTTP_LUA_SHDICT_H_INCLUDED_
#define _TS_HTTP_LUA_SHDICT_H_INCLUDED_

#include <inttypes.h>

#include "ts_lua_base_rbtree.h"
#include "ts_lua_base_queue.h"
#include "ts_lua_common.h"
#include "ts_lua_slab.h"

typedef struct {
  unsigned char color;
  uint8_t value_type;
  unsigned short key_len;
  uint32_t value_len;
  uint64_t expires;
  ts_queue_t queue;
  uint32_t user_flags;
  unsigned char data[1];
} ts_http_lua_shdict_node_t;

typedef struct {
  ts_queue_t queue;
  uint32_t value_len;
  uint8_t value_type;
  unsigned char data[1];
} ts_http_lua_shdict_list_node_t;

typedef struct {
  ts_rbtree_t rbtree;
  ts_rbtree_node_t sentinel;
  ts_queue_t lru_queue;
} ts_http_lua_shdict_shctx_t;

typedef struct {
  ts_http_lua_shdict_shctx_t *sh;
  ts_slab_pool_t *shpool;
  char *name;
  //    ts_http_lua_main_conf_t     *main_conf;
} ts_http_lua_shdict_ctx_t;

// typedef struct {
//    ts_http_lua_main_conf_t    *lmcf;
//    ts_shm_zone_t               zone;
// } ts_http_lua_shm_zone_ctx_t;

// int ts_http_lua_shdict_init_zone(ts_shm_zone_t *shm_zone, void *data);
// void ts_http_lua_shdict_rbtree_insert_value(ts_rbtree_node_t *temp,
//     ts_rbtree_node_t *node, ts_rbtree_node_t *sentinel);
// void ts_http_lua_inject_shdict_api(ts_http_lua_main_conf_t *lmcf,
//    lua_State *L);
ts_http_lua_shdict_ctx_t *ts_http_lua_shdict_init_zone(const char *name, const int len, size_t size);
void ts_http_lua_inject_shdict_api(lua_State *L);

static inline int
ts_memn2cmp(unsigned char *s1, unsigned char *s2, size_t n1, size_t n2)
{
  size_t n;
  int m, z;

  if (n1 <= n2) {
    n = n1;
    z = -1;

  } else {
    n = n2;
    z = 1;
  }

  m = memcmp((const char *)s1, (const char *)s2, n);

  if (m || n1 == n2) {
    return m;
  }

  return z;
}

#endif /* _TS_HTTP_LUA_SHDICT_H_INCLUDED_ */

/* vi:set ft=c ts=4 sw=4 et fdm=marker: */
