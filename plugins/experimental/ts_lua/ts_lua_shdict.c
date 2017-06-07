#include <stdio.h>
#include <stdlib.h>

#include "ts_lua_slab.h"
#include "ts_lua_shdict.h"
#include "ts_lua_common.h"
#include "ts_lua_base_crc32.h"

#define SHDICT_OK 0
#define SHDICT_ERROR -1
#define SHDICT_DONE -4
#define SHDICT_DECLINED -5

#define DEBUG_TAG "shdict"

#define TS_HTTP_LUA_SHDICT_ADD 0x0001
#define TS_HTTP_LUA_SHDICT_REPLACE 0x0002
#define TS_HTTP_LUA_SHDICT_SAFE_STORE 0x0004

#define TS_HTTP_LUA_SHDICT_LEFT 0x0001
#define TS_HTTP_LUA_SHDICT_RIGHT 0x0002

#define TS_ALIGNMENT sizeof(unsigned long)

static ts_http_lua_shdict_ctx_t *ts_http_lua_shdict_do_init_zone(ts_http_lua_shdict_ctx_t *ctx);
static int ts_http_lua_shdict_expire(ts_http_lua_shdict_ctx_t *ctx, unsigned int n);
static int ts_http_lua_shdict_set_helper(lua_State *L, int flags);
static int ts_http_lua_shdict_get_helper(lua_State *L, int get_stale);
static int ts_http_lua_shdict_lookup(ts_http_lua_shdict_ctx_t *shm_zone, unsigned int hash, unsigned char *kdata, size_t klen,
                                     ts_http_lua_shdict_node_t **sdp);

enum {
  SHDICT_USERDATA_INDEX = 1,
};

enum {
  SHDICT_TNIL     = 0, /* same as LUA_TNIL */
  SHDICT_TBOOLEAN = 1, /* same as LUA_TBOOLEAN */
  SHDICT_TNUMBER  = 3, /* same as LUA_TNUMBER */
  SHDICT_TSTRING  = 4, /* same as LUA_TSTRING */
  SHDICT_TLIST    = 5,
};

static inline ts_http_lua_shdict_ctx_t *
ts_http_lua_get_ctx(lua_State *L, int index)
{
  ts_http_lua_shdict_ctx_t *ctx;
  lua_rawgeti(L, index, SHDICT_USERDATA_INDEX);
  ctx = lua_touserdata(L, -1);
  lua_pop(L, 1);

  return ctx;
}

static inline ts_queue_t *
ts_http_lua_shdict_get_list_head(ts_http_lua_shdict_node_t *sd, size_t len)
{
  return (ts_queue_t *)ts_align_ptr(((unsigned char *)&sd->data + len), TS_ALIGNMENT);
}

ts_http_lua_shdict_ctx_t *
ts_http_lua_shdict_init_zone(const char *name, const int len, size_t size)
{
  ts_http_lua_shdict_ctx_t *ctx;
  ts_slab_pool_t *shpool;
  shpool = ts_slab_pool_init(size);
  if (!shpool) {
    TSError("[%s] cannot init share pool", "ts_lua");
    return NULL;
  }

  ctx = (ts_http_lua_shdict_ctx_t *)TSmalloc(sizeof(ts_http_lua_shdict_ctx_t));
  memset(ctx, 0, sizeof(ts_http_lua_shdict_ctx_t));

  shpool->data   = ctx;
  ctx->shpool    = shpool;
  ctx->name      = (char *)TSmalloc(len + 1);
  ctx->name[len] = 0;
  memcpy(ctx->name, name, len);
  return ts_http_lua_shdict_do_init_zone(ctx);
}

void
ts_http_lua_shdict_rbtree_insert_value(ts_rbtree_node_t *temp, ts_rbtree_node_t *node, ts_rbtree_node_t *sentinel)
{
  ts_rbtree_node_t **p;
  ts_http_lua_shdict_node_t *sdn, *sdnt;

  for (;;) {
    if (node->key < temp->key) {
      p = &temp->left;

    } else if (node->key > temp->key) {
      p = &temp->right;

    } else { /* node->key == temp->key */

      sdn  = (ts_http_lua_shdict_node_t *)&node->color;
      sdnt = (ts_http_lua_shdict_node_t *)&temp->color;

      p = ts_memn2cmp(sdn->data, sdnt->data, sdn->key_len, sdnt->key_len) < 0 ? &temp->left : &temp->right;
    }

    if (*p == sentinel) {
      break;
    }

    temp = *p;
  }

  *p           = node;
  node->parent = temp;
  node->left   = sentinel;
  node->right  = sentinel;
  ts_rbt_red(node);
}

static int
ts_http_lua_shdict_flush_expired(lua_State *L)
{
  return 0;
}

static int
ts_http_lua_shdict_set(lua_State *L)
{
  return ts_http_lua_shdict_set_helper(L, 0);
}

static int
ts_http_lua_shdict_safe_set(lua_State *L)
{
  return ts_http_lua_shdict_set_helper(L, TS_HTTP_LUA_SHDICT_SAFE_STORE);
}

static int
ts_http_lua_shdict_add(lua_State *L)
{
  return ts_http_lua_shdict_set_helper(L, TS_HTTP_LUA_SHDICT_ADD);
}

static int
ts_http_lua_shdict_safe_add(lua_State *L)
{
  return ts_http_lua_shdict_set_helper(L, TS_HTTP_LUA_SHDICT_ADD | TS_HTTP_LUA_SHDICT_SAFE_STORE);
}

static int
ts_http_lua_shdict_delete(lua_State *L)
{
  int n;

  n = lua_gettop(L);

  if (n != 2) {
    return luaL_error(L, "expecting 2 arguments, "
                         "but only seen %d",
                      n);
  }

  lua_pushnil(L);

  return ts_http_lua_shdict_set_helper(L, 0);
}

static int
ts_http_lua_shdict_replace(lua_State *L)
{
  return ts_http_lua_shdict_set_helper(L, TS_HTTP_LUA_SHDICT_REPLACE);
}

static int
ts_http_lua_shdict_incr(lua_State *L)
{
  return 0;
}

static int
ts_http_lua_shdict_lpush(lua_State *L)
{
  return 0;
}

static int
ts_http_lua_shdict_rpush(lua_State *L)
{
  return 0;
}

static int
ts_http_lua_shdict_lpop(lua_State *L)
{
  return 0;
}

static int
ts_http_lua_shdict_rpop(lua_State *L)
{
  return 0;
}

static int
ts_http_lua_shdict_llen(lua_State *L)
{
  return 0;
}

static int
ts_http_lua_shdict_flush_all(lua_State *L)
{
  return 0;
}

static int
ts_http_lua_shdict_get_keys(lua_State *L)
{
  return 0;
}

static int
ts_http_lua_shdict_get(lua_State *L)
{
  return ts_http_lua_shdict_get_helper(L, 0 /* stale */);
}

static int
ts_http_lua_shdict_get_stale(lua_State *L)
{
  return ts_http_lua_shdict_get_helper(L, 1 /* stale */);
}

static int
ts_http_lua_shdict_get_helper(lua_State *L, int get_stale)
{
  int n;
  char *name;
  unsigned char *key;
  unsigned char *value;
  size_t key_len;
  size_t value_len;
  uint32_t hash;
  int rc;
  ts_http_lua_shdict_ctx_t *ctx;
  ts_http_lua_shdict_node_t *sd;
  int value_type;
  double num;
  unsigned char c;
  uint32_t user_flags = 0;

  n = lua_gettop(L);

  if (n != 2) {
    return luaL_error(L, "expecting exactly two arguments, "
                         "but only seen %d",
                      n);
  }

  if (lua_type(L, 1) != LUA_TTABLE) {
    return luaL_error(L, "bad \"zone\" argument");
  }

  ctx = ts_http_lua_get_ctx(L, 1);
  if (ctx == NULL) {
    return luaL_error(L, "bad \"zone\" argument");
  }

  name = ctx->name;

  if (lua_isnil(L, 2)) {
    lua_pushnil(L);
    lua_pushliteral(L, "nil key");
    return 2;
  }

  key = (unsigned char *)luaL_checklstring(L, 2, &key_len);

  if (key_len == 0) {
    lua_pushnil(L);
    lua_pushliteral(L, "empty key");
    return 2;
  }

  if (key_len > 65535) {
    lua_pushnil(L);
    lua_pushliteral(L, "key too long");
    return 2;
  }

  hash = ts_crc32_short(key, key_len);

#if (TS_DEBUG)
// TSDebug(DEBUG_TAG, "fetching key \"%s\" in shared dict \"%s\"", key, name);
#endif /* TS_DEBUG */

  TSMutexLock(ctx->shpool->mutex);

#if 1
  if (!get_stale) {
    ts_http_lua_shdict_expire(ctx, 1);
  }
#endif

  rc = ts_http_lua_shdict_lookup(ctx, hash, key, key_len, &sd);

  TSDebug(DEBUG_TAG, "shdict lookup returns %d", (int)rc);

  if (rc == SHDICT_DECLINED || (rc == SHDICT_DONE && !get_stale)) {
    TSMutexUnlock(ctx->shpool->mutex);
    lua_pushnil(L);
    return 1;
  }

  /* rc == SHDICT_OK || (rc == SHDICT_DONE && get_stale) */

  value_type = sd->value_type;

  TSDebug(DEBUG_TAG, "data: %p", sd->data);
  TSDebug(DEBUG_TAG, "key len: %d", (int)sd->key_len);

  value     = sd->data + sd->key_len;
  value_len = (size_t)sd->value_len;

  switch (value_type) {
  case SHDICT_TSTRING:

    lua_pushlstring(L, (char *)value, value_len);
    break;

  case SHDICT_TNUMBER:

    if (value_len != sizeof(double)) {
      TSMutexUnlock(ctx->shpool->mutex);

      return luaL_error(L, "bad lua number value size found for key %s "
                           "in shared_dict %s: %lu",
                        key, name, (unsigned long)value_len);
    }

    memcpy(&num, value, sizeof(double));

    lua_pushnumber(L, num);
    break;

  case SHDICT_TBOOLEAN:

    if (value_len != sizeof(unsigned char)) {
      TSMutexUnlock(ctx->shpool->mutex);

      return luaL_error(L, "bad lua boolean value size found for key %s "
                           "in shared_dict %s: %lu",
                        key, name, (unsigned long)value_len);
    }

    c = *value;

    lua_pushboolean(L, c ? 1 : 0);
    break;

  case SHDICT_TLIST:

    TSMutexUnlock(ctx->shpool->mutex);

    lua_pushnil(L);
    lua_pushliteral(L, "value is a list");
    return 2;

  default:

    TSMutexUnlock(ctx->shpool->mutex);

    return luaL_error(L, "bad value type found for key %s in "
                         "shared_dict %s: %d",
                      key, name, value_type);
  }

  user_flags = sd->user_flags;

  TSMutexUnlock(ctx->shpool->mutex);

  if (get_stale) {
    /* always return value, flags, stale */

    if (user_flags) {
      lua_pushinteger(L, (lua_Integer)user_flags);

    } else {
      lua_pushnil(L);
    }

    lua_pushboolean(L, rc == SHDICT_DONE);
    return 3;
  }

  if (user_flags) {
    lua_pushinteger(L, (lua_Integer)user_flags);
    return 2;
  }

  return 1;
}

static int
ts_http_lua_shdict_set_helper(lua_State *L, int flags)
{
  int i, n;
  unsigned char *key;
  size_t key_len;
  uint32_t hash;
  int rc;
  ts_http_lua_shdict_ctx_t *ctx;
  ts_http_lua_shdict_node_t *sd;
  unsigned char *value;
  size_t value_len;
  int value_type;
  double num;
  unsigned char c;
  lua_Number exptime = 0;
  unsigned char *p;
  ts_rbtree_node_t *node;
  TSHRTime tp;
  // ts_shm_zone_t              *zone;
  int forcible = 0;
  /* indicates whether to foricibly override other
   * valid entries */
  int32_t user_flags = 0;
  ts_queue_t *queue, *q;

  n = lua_gettop(L);

  if (n != 3 && n != 4 && n != 5) {
    return luaL_error(L, "expecting 3, 4 or 5 arguments, "
                         "but only seen %d",
                      n);
  }

  if (lua_type(L, 1) != LUA_TTABLE) {
    return luaL_error(L, "bad \"zone\" argument");
  }

  ctx = ts_http_lua_get_ctx(L, 1);
  if (ctx == NULL) {
    return luaL_error(L, "bad \"zone\" argument");
  }

  if (lua_isnil(L, 2)) {
    lua_pushnil(L);
    lua_pushliteral(L, "nil key");
    return 2;
  }

  key = (unsigned char *)luaL_checklstring(L, 2, &key_len);
  if (key_len == 0) {
    lua_pushnil(L);
    lua_pushliteral(L, "empty key");
    return 2;
  }

  if (key_len > 65535) {
    lua_pushnil(L);
    lua_pushliteral(L, "key too long");
    return 2;
  }

  hash = ts_crc32_short(key, key_len);

  value_type = lua_type(L, 3);

  switch (value_type) {
  case SHDICT_TSTRING:
    value = (unsigned char *)lua_tolstring(L, 3, &value_len);
    break;

  case SHDICT_TNUMBER:
    value_len = sizeof(double);
    num       = lua_tonumber(L, 3);
    value     = (unsigned char *)&num;
    break;

  case SHDICT_TBOOLEAN:
    value_len = sizeof(unsigned char);
    c         = lua_toboolean(L, 3) ? 1 : 0;
    value     = &c;
    break;

  case LUA_TNIL:
    if (flags & (TS_HTTP_LUA_SHDICT_ADD | TS_HTTP_LUA_SHDICT_REPLACE)) {
      lua_pushnil(L);
      lua_pushliteral(L, "attempt to add or replace nil values");
      return 2;
    }

    value     = NULL;
    value_len = 0;
    break;

  default:
    lua_pushnil(L);
    lua_pushliteral(L, "bad value type");
    return 2;
  }

  if (n >= 4) {
    exptime = luaL_checknumber(L, 4);
    if (exptime < 0) {
      return luaL_error(L, "bad \"exptime\" argument");
    }
  }

  if (n == 5) {
    user_flags = (uint32_t)luaL_checkinteger(L, 5);
  }

  TSMutexLock(ctx->shpool->mutex);

#if 1
  ts_http_lua_shdict_expire(ctx, 1);
#endif

  rc = ts_http_lua_shdict_lookup(ctx, hash, key, key_len, &sd);

  TSDebug(DEBUG_TAG, "shdict lookup returned %d", (int)rc);

  if (flags & TS_HTTP_LUA_SHDICT_REPLACE) {
    if (rc == SHDICT_DECLINED || rc == SHDICT_DONE) {
      TSMutexUnlock(ctx->shpool->mutex);

      lua_pushboolean(L, 0);
      lua_pushliteral(L, "not found");
      lua_pushboolean(L, forcible);
      return 3;
    }

    /* rc == SHDICT_OK */

    goto replace;
  }

  if (flags & TS_HTTP_LUA_SHDICT_ADD) {
    if (rc == SHDICT_OK) {
      TSMutexUnlock(ctx->shpool->mutex);

      lua_pushboolean(L, 0);
      lua_pushliteral(L, "exists");
      lua_pushboolean(L, forcible);
      return 3;
    }

    if (rc == SHDICT_DONE) {
      /* exists but expired */

      TSDebug(DEBUG_TAG, "go to replace");
      goto replace;
    }

    /* rc == SHDICT_DECLINED */

    TSDebug(DEBUG_TAG, "go to insert");
    goto insert;
  }

  if (rc == SHDICT_OK || rc == SHDICT_DONE) {
    if (value_type == LUA_TNIL) {
      goto remove;
    }

  replace:

    if (value && value_len == (size_t)sd->value_len && sd->value_type != SHDICT_TLIST) {
      TSDebug(DEBUG_TAG, "lua shared dict set: found old entry and value "
                         "size matched, reusing it");

      ts_queue_remove(&sd->queue);
      ts_queue_insert_head(&ctx->sh->lru_queue, &sd->queue);

      sd->key_len = (u_short)key_len;

      if (exptime > 0) {
        tp          = TShrtime();
        sd->expires = (uint64_t)tp + (uint64_t)(exptime * TS_HRTIME_SECOND);

      } else {
        sd->expires = 0;
      }

      sd->user_flags = user_flags;

      sd->value_len = (uint32_t)value_len;

      TSDebug(DEBUG_TAG, "setting value type to %d", value_type);

      sd->value_type = (uint8_t)value_type;

      p = memcpy(sd->data, key, key_len) + key_len;
      memcpy(p, value, value_len);

      TSMutexUnlock(ctx->shpool->mutex);

      lua_pushboolean(L, 1);
      lua_pushnil(L);
      lua_pushboolean(L, forcible);
      return 3;
    }

    TSDebug(DEBUG_TAG, "lua shared dict set: found old entry but value size "
                       "NOT matched, removing it first");

  remove:

    if (sd->value_type == SHDICT_TLIST) {
      queue = ts_http_lua_shdict_get_list_head(sd, key_len);

      for (q = ts_queue_head(queue); q != ts_queue_sentinel(queue); q = ts_queue_next(q)) {
        p = (unsigned char *)ts_queue_data(q, ts_http_lua_shdict_list_node_t, queue);

        ts_slab_free_locked(ctx->shpool, p);
      }
    }

    ts_queue_remove(&sd->queue);

    node = (ts_rbtree_node_t *)((unsigned char *)sd - offsetof(ts_rbtree_node_t, color));

    ts_rbtree_delete(&ctx->sh->rbtree, node);

    ts_slab_free_locked(ctx->shpool, node);
  }

insert:

  /* rc == SHDICT_DECLINED or value size unmatch */

  if (value == NULL) {
    TSMutexUnlock(ctx->shpool->mutex);

    lua_pushboolean(L, 1);
    lua_pushnil(L);
    lua_pushboolean(L, 0);
    return 3;
  }

  TSDebug(DEBUG_TAG, "lua shared dict set: creating a new entry");

  n = offsetof(ts_rbtree_node_t, color) + offsetof(ts_http_lua_shdict_node_t, data) + key_len + value_len;

  TSDebug(DEBUG_TAG, "overhead = %d", (int)(offsetof(ts_rbtree_node_t, color) + offsetof(ts_http_lua_shdict_node_t, data)));

  node = ts_slab_alloc_locked(ctx->shpool, n);

  if (node == NULL) {
    if (flags & TS_HTTP_LUA_SHDICT_SAFE_STORE) {
      TSMutexUnlock(ctx->shpool->mutex);

      lua_pushboolean(L, 0);
      lua_pushliteral(L, "no memory");
      return 2;
    }

    TSDebug(DEBUG_TAG, "lua shared dict set: overriding non-expired items "
                       "due to memory shortage for entry \"%s\"",
            key);

    for (i = 0; i < 30; i++) {
      if (ts_http_lua_shdict_expire(ctx, 0) == 0) {
        break;
      }

      forcible = 1;

      node = ts_slab_alloc_locked(ctx->shpool, n);
      if (node != NULL) {
        goto allocated;
      }
    }

    TSMutexUnlock(ctx->shpool->mutex);

    lua_pushboolean(L, 0);
    lua_pushliteral(L, "no memory");
    lua_pushboolean(L, forcible);
    return 3;
  }

allocated:

  sd = (ts_http_lua_shdict_node_t *)&node->color;

  node->key   = hash;
  sd->key_len = (u_short)key_len;

  if (exptime > 0) {
    tp          = TShrtime();
    sd->expires = (uint64_t)tp + (uint64_t)(exptime * TS_HRTIME_SECOND);

  } else {
    sd->expires = 0;
  }

  sd->user_flags = user_flags;

  sd->value_len = (uint32_t)value_len;

  TSDebug(DEBUG_TAG, "setting value type to %d", value_type);

  sd->value_type = (uint8_t)value_type;

  p = memcpy(sd->data, key, key_len) + key_len;
  memcpy(p, value, value_len);

  ts_rbtree_insert(&ctx->sh->rbtree, node);

  ts_queue_insert_head(&ctx->sh->lru_queue, &sd->queue);

  TSMutexUnlock(ctx->shpool->mutex);

  lua_pushboolean(L, 1);
  lua_pushnil(L);
  lua_pushboolean(L, forcible);
  return 3;
}

static int
ts_http_lua_shdict_expire(ts_http_lua_shdict_ctx_t *ctx, unsigned int n)
{
  TSHRTime now;
  ts_queue_t *q, *list_queue, *lq;
  int64_t ms;
  ts_rbtree_node_t *node;
  ts_http_lua_shdict_node_t *sd;
  int freed = 0;
  ts_http_lua_shdict_list_node_t *lnode;

  now = TShrtime();

  /*
   * n == 1 deletes one or two expired entries
   * n == 0 deletes oldest entry by force
   *        and one or two zero rate entries
   */

  while (n < 3) {
    if (ts_queue_empty(&ctx->sh->lru_queue)) {
      return freed;
    }

    q = ts_queue_last(&ctx->sh->lru_queue);

    sd = ts_queue_data(q, ts_http_lua_shdict_node_t, queue);

    if (n++ != 0) {
      if (sd->expires == 0) {
        return freed;
      }

      ms = sd->expires - now;
      if (ms > 0) {
        return freed;
      }
    }

    if (sd->value_type == SHDICT_TLIST) {
      list_queue = ts_http_lua_shdict_get_list_head(sd, sd->key_len);

      for (lq = ts_queue_head(list_queue); lq != ts_queue_sentinel(list_queue); lq = ts_queue_next(lq)) {
        lnode = ts_queue_data(lq, ts_http_lua_shdict_list_node_t, queue);

        ts_slab_free_locked(ctx->shpool, lnode);
      }
    }

    ts_queue_remove(q);

    node = (ts_rbtree_node_t *)((unsigned char *)sd - offsetof(ts_rbtree_node_t, color));

    ts_rbtree_delete(&ctx->sh->rbtree, node);

    ts_slab_free_locked(ctx->shpool, node);

    freed++;
  }

  return freed;
}

static int
ts_http_lua_shdict_lookup(ts_http_lua_shdict_ctx_t *ctx, unsigned int hash, unsigned char *kdata, size_t klen,
                          ts_http_lua_shdict_node_t **sdp)
{
  int rc;
  TSHRTime now;
  int64_t ms;
  ts_rbtree_node_t *node, *sentinel;
  ts_http_lua_shdict_node_t *sd;

  node     = ctx->sh->rbtree.root;
  sentinel = ctx->sh->rbtree.sentinel;

  while (node != sentinel) {
    if (hash < node->key) {
      node = node->left;
      continue;
    }

    if (hash > node->key) {
      node = node->right;
      continue;
    }

    /* hash == node->key */

    sd = (ts_http_lua_shdict_node_t *)&node->color;

    rc = ts_memn2cmp(kdata, sd->data, klen, (size_t)sd->key_len);

    if (rc == 0) {
      ts_queue_remove(&sd->queue);
      ts_queue_insert_head(&ctx->sh->lru_queue, &sd->queue);

      *sdp = sd;

      TSDebug(DEBUG_TAG, "node expires: %lld", (long long)sd->expires);

      if (sd->expires != 0) {
        now = TShrtime();
        ms  = sd->expires - now;

        TSDebug(DEBUG_TAG, "time to live: %lld", (long long)ms);

        if (ms < 0) {
          TSDebug(DEBUG_TAG, "node already expired");
          return SHDICT_DONE;
        }
      }

      return SHDICT_OK;
    }

    node = (rc < 0) ? node->left : node->right;
  }

  *sdp = NULL;

  return SHDICT_DECLINED;
}

void
ts_http_lua_inject_shdict_api(lua_State *L)
{
  int i;
  int pool_len = get_global_pool_len();
  if (pool_len) {
    lua_createtable(L, 0, pool_len /* nrec */); // tb1

    lua_createtable(L, 0 /* narr */, 18 /* nrec */); // shared mt tb2

    lua_pushcfunction(L, ts_http_lua_shdict_get);
    lua_setfield(L, -2, "get"); // tb2.get = ts_http_lua_shdict_get

    lua_pushcfunction(L, ts_http_lua_shdict_get_stale);
    lua_setfield(L, -2, "get_stale"); // tb2.get_stale = ts_http_lua_shdict_get_stale

    lua_pushcfunction(L, ts_http_lua_shdict_set);
    lua_setfield(L, -2, "set"); //	tb2.set = ts_http_lua_shdict_set

    lua_pushcfunction(L, ts_http_lua_shdict_safe_set);
    lua_setfield(L, -2, "safe_set"); //	tb2.safe_set = ts_http_lua_shdict_safe_set

    lua_pushcfunction(L, ts_http_lua_shdict_add);
    lua_setfield(L, -2, "add"); //	tb2.add  = ts_http_lua_shdict_add

    lua_pushcfunction(L, ts_http_lua_shdict_safe_add);
    lua_setfield(L, -2, "safe_add"); //	tb2.safe_add = ts_http_lua_shdict_safe_add

    lua_pushcfunction(L, ts_http_lua_shdict_replace);
    lua_setfield(L, -2, "replace"); //	tb2.replace  = ts_http_lua_shdict_replace

    lua_pushcfunction(L, ts_http_lua_shdict_incr);
    lua_setfield(L, -2, "incr"); //	tb2.incr 	 =	ts_http_lua_shdict_incr

    lua_pushcfunction(L, ts_http_lua_shdict_delete);
    lua_setfield(L, -2, "delete"); //	tb2.delete	 = ts_http_lua_shdict_delete

    lua_pushcfunction(L, ts_http_lua_shdict_lpush);
    lua_setfield(L, -2, "lpush"); //	tb2.lpush 	 = ts_http_lua_shdict_lpush

    lua_pushcfunction(L, ts_http_lua_shdict_rpush);
    lua_setfield(L, -2, "rpush"); // tb2.rpush 	 = ts_http_lua_shdict_rpush

    lua_pushcfunction(L, ts_http_lua_shdict_lpop);
    lua_setfield(L, -2, "lpop"); // tb2.lpop 	= ts_http_lua_shdict_lpop

    lua_pushcfunction(L, ts_http_lua_shdict_rpop);
    lua_setfield(L, -2, "rpop"); // tb2.rpop 	=	ts_http_lua_shdict_rpop

    lua_pushcfunction(L, ts_http_lua_shdict_llen);
    lua_setfield(L, -2, "llen"); // tb2.llen  	= 	ts_http_lua_shdict_llen

    lua_pushcfunction(L, ts_http_lua_shdict_flush_all);
    lua_setfield(L, -2, "flush_all"); // tb2.flush_all	=	ts_http_lua_shdict_flush_all

    lua_pushcfunction(L, ts_http_lua_shdict_flush_expired);
    lua_setfield(L, -2, "flush_expired"); // tb2.flush_expired	=	ts_http_lua_shdict_flush_expired

    lua_pushcfunction(L, ts_http_lua_shdict_get_keys);
    lua_setfield(L, -2, "get_keys"); // tb2.get_keys			=	ts_http_lua_shdict_get_keys

    lua_pushvalue(L, -1);           // tb3 = tb2
    lua_setfield(L, -2, "__index"); // tb2.__index = tb3

    ts_slab_pool_t **pool = get_global_pool();
    for (i = 0; i < pool_len; i++) {
      ts_http_lua_shdict_ctx_t *ctx = (ts_http_lua_shdict_ctx_t *)pool[i]->data;

      lua_pushlstring(L, (char *)ctx->name, strlen(ctx->name));

      lua_createtable(L, 1 /* narr */, 0 /* nrec */); // 	tb4
      lua_pushlightuserdata(L, ctx);
      lua_rawseti(L, -2, 1);   //	tb4[1] =  userdata
      lua_pushvalue(L, -3);    /* shared mt key ud mt */
      lua_setmetatable(L, -2); /* shared mt key ud */
      lua_rawset(L, -4);       /* shared mt */
    }

    lua_pop(L, 1); /* shared */

  } else {
    lua_newtable(L); /* ts.shared */
  }

  lua_setfield(L, -2, "shared");
}

static ts_http_lua_shdict_ctx_t *
ts_http_lua_shdict_do_init_zone(ts_http_lua_shdict_ctx_t *ctx)
{
  ctx->sh = ts_slab_alloc(ctx->shpool, sizeof(ts_http_lua_shdict_shctx_t));
  if (ctx->sh == NULL) {
    return NULL;
  }

  ts_rbtree_init(&ctx->sh->rbtree, &ctx->sh->sentinel, ts_http_lua_shdict_rbtree_insert_value);

  ts_queue_init(&ctx->sh->lru_queue);

  TSDebug("DEBUG_TAG", " in lua_shared_dict zone %s", ctx->name);

  return ctx;
}
