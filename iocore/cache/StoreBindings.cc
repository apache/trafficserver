/** @file
 *
 *  A brief file description
 *
 *  @section license License
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#include "StoreBindings.h"
#include "P_Cache.h"
#include "ts/I_Layout.h"

static int
create_store_object(lua_State *L)
{
  const char *path;
  const char *id;
  lua_Integer volume;
  const char *size_str;
  int64_t size    = -1;
  const char *err = NULL;

  Store *store = (Store *)BindingInstance::self(L)->retrieve_ptr("store.config");

  BindingInstance::typecheck(L, "store", LUA_TTABLE, LUA_TNONE);

  path     = lua_getfield<const char *>(L, -1, "Path", NULL);
  id       = lua_getfield<const char *>(L, -1, "Id", NULL);
  volume   = lua_getfield<lua_Integer>(L, -1, "Volume", 0);
  size_str = lua_getfield<const char *>(L, -1, "Size", NULL);

  if (path == NULL) {
    return luaL_error(L, "missing or invalid 'Path' argument");
  }

  if (size_str == NULL) {
    return luaL_error(L, "missing or invalid 'Size' argument");
  }

  if (ParseRules::is_digit(*size_str)) {
    if ((size = ink_atoi64(size_str)) <= 0) {
      return luaL_error(L, "error parsing size");
    }
  }

  if (id != NULL) {
    if (ParseRules::is_space(*id)) {
      id = NULL;
    }
  }

  if (volume < 0) {
    return luaL_error(L, "error parsing volume number");
  }

  char *pp = Layout::get()->relative(path);

  Span *ns = new Span;
  Debug("lua", "Store::evaluate_config - new Span; ns->init(\"%s\",%" PRId64 "), forced volume=%td%s%s", pp, size, volume,
        id ? " id=" : "", id ? id : "");
  if ((err = ns->init(pp, size))) {
    RecSignalWarning(REC_SIGNAL_SYSTEM_ERROR, "could not initialize storage \"%s\" [%s]", pp, err);
    Debug("lua", "Store::evaluate_config - could not initialize storage \"%s\" [%s]", pp, err);
    delete ns;
    ats_free(pp);
    return luaL_error(L, "Store::evaluate_config - could not initialize storage");
  }
  ats_free(pp);
  store->n_disks_in_config++;

  // Set side values if present.
  if (id)
    ns->hash_base_string_set(id);
  if (volume > 0)
    ns->volume_number_set(volume);

  // new Span
  {
    Span *prev       = store->curr_span;
    store->curr_span = ns;
    if (!store->span_head)
      store->span_head = store->curr_span;
    else
      prev->link.next = store->curr_span;
  }

  lua_pushnil(L);
  return 1;
}

bool
MakeStoreBindings(BindingInstance &binding, Store *store)
{
  binding.bind_function("store", create_store_object);

  // Attach the Store backpointer.
  binding.attach_ptr("store.config", store);

  return true;
}
