/** @file

  Memory allocation routines for libts

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "ts/jemallctl.h"

#include "ts/ink_platform.h"

// includes jemalloc.h
#include "ts/ink_memory.h"
#include "ts/ink_defs.h"
#include "ts/ink_stack_trace.h"
#include "ts/Diags.h"
#include "ts/ink_atomic.h"
#include "ts/ink_align.h"

#include <cassert>
#if defined(linux) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 600
#endif

#include <vector>
#include <cstdlib>
#include <cstring>

#include <string>

namespace jemallctl
{
// internal read/write functions

int mallctl_void(const objpath_t &oid);

template <typename T_VALUE> auto mallctl_get(const objpath_t &oid) -> T_VALUE;

template <typename T_VALUE> auto mallctl_set(const objpath_t &oid, const T_VALUE &v) -> int;

// define object functors (to allow instances below)

objpath_t objpath(const std::string &path);

ObjBase::ObjBase(const char *name) : _oid(objpath(name))
{
}

template <typename T_VALUE, size_t N_DIFF>
auto
GetObjFxn<T_VALUE, N_DIFF>::operator()(void) const -> T_VALUE
{
  return std::move(::jemallctl::mallctl_get<T_VALUE>(ObjBase::_oid));
}

template <typename T_VALUE, size_t N_DIFF>
auto
SetObjFxn<T_VALUE, N_DIFF>::operator()(const T_VALUE &v) const -> int
{
  return ::jemallctl::mallctl_set(ObjBase::_oid, v);
}

int
GetObjFxn<void, 0>::operator()(void) const
{
  return ::jemallctl::mallctl_void(ObjBase::_oid);
}

#if !HAVE_LIBJEMALLOC
objpath_t
objpath(const std::string &path)
{
  return objpath_t();
}
auto
mallctl_void(const objpath_t &oid) -> int
{
  return -1;
}
template <typename T_VALUE>
auto
mallctl_set(const objpath_t &oid, const T_VALUE &v) -> int
{
  return -1;
}
template <typename T_VALUE>
auto
mallctl_get(const objpath_t &oid) -> T_VALUE
{
  return std::move(T_VALUE{});
}

#else

objpath_t
objpath(const std::string &path)
{
  objpath_t oid;

  oid.resize(10); // longer than any oid target// *explicitly resize*
  size_t len = oid.size();
  mallctlnametomib(path.c_str(), oid.data(), &len);
  oid.resize(len);
  return std::move(oid);
}

/// implementations for ObjBase

auto
mallctl_void(const objpath_t &oid) -> int
{
  return mallctlbymib(oid.data(), oid.size(), nullptr, nullptr, nullptr, 0);
}

template <typename T_VALUE>
auto
mallctl_set(const objpath_t &oid, const T_VALUE &v) -> int
{
  return mallctlbymib(oid.data(), oid.size(), nullptr, nullptr, const_cast<T_VALUE *>(&v), sizeof(v));
}

template <typename T_VALUE>
auto
mallctl_get(const objpath_t &oid) -> T_VALUE
{
  T_VALUE v{}; // init to zero if a pod type
  size_t len = sizeof(v);
  mallctlbymib(oid.data(), oid.size(), &v, &len, nullptr, 0);
  return std::move(v);
}

template <>
auto
mallctl_get<std::string>(const objpath_t &oid) -> std::string
{
  char buff[256]; // adequate for paths and most things
  size_t len = sizeof(buff);
  mallctlbymib(oid.data(), oid.size(), &buff, &len, nullptr, 0);
  std::string v(&buff[0], len); // copy out
  return std::move(v);
}

template <>
auto
mallctl_set<std::string>(const objpath_t &oid, const std::string &v) -> int
{
  return mallctlbymib(oid.data(), oid.size(), nullptr, nullptr, const_cast<char *>(v.c_str()), v.size());
}

template <>
auto
mallctl_get<chunk_hooks_t>(const objpath_t &baseOid) -> chunk_hooks_t
{
  objpath_t oid = baseOid;
  oid[1]        = thread_arena();

  chunk_hooks_t v;
  size_t len = sizeof(v);
  mallctlbymib(oid.data(), oid.size(), &v, &len, nullptr, 0);
  return std::move(v);
}

template <>
auto
mallctl_set<chunk_hooks_t>(const objpath_t &baseOid, const chunk_hooks_t &hooks) -> int
{
  objpath_t oid = baseOid;
  oid[1]        = ::jemallctl::thread_arena();
  auto ohooks   = mallctl_get<chunk_hooks_t>(oid);
  auto nhooks = chunk_hooks_t{(hooks.alloc ?: ohooks.alloc),       (hooks.dalloc ?: ohooks.dalloc), (hooks.commit ?: ohooks.commit),
                              (hooks.decommit ?: ohooks.decommit), (hooks.purge ?: ohooks.purge),   (hooks.split ?: ohooks.split),
                              (hooks.merge ?: ohooks.merge)};
  return mallctlbymib(oid.data(), oid.size(), nullptr, nullptr, const_cast<chunk_hooks_t *>(&nhooks), sizeof(nhooks));
}
#endif

template struct GetObjFxn<uint64_t>;
template struct GetObjFxn<unsigned>;
template struct GetObjFxn<bool>;
template struct GetObjFxn<chunk_hooks_t>;

template struct SetObjFxn<uint64_t>;
template struct SetObjFxn<unsigned>;
template struct SetObjFxn<bool>;
template struct SetObjFxn<chunk_hooks_t>;

const GetObjFxn<chunk_hooks_t> thread_arena_hooks{"arena.0.chunk_hooks"};
const SetObjFxn<chunk_hooks_t> set_thread_arena_hooks{"arena.0.chunk_hooks"};

// request-or-sense new values in statistics
const GetObjFxn<uint64_t> epoch{"epoch"};

// request separated page sets for each NUMA node (when created)
const GetObjFxn<unsigned> do_arenas_extend{"arenas.extend"}; // unsigned r-

// assigned arena for local thread
const GetObjFxn<unsigned> thread_arena{"thread.arena"};     // unsigned rw
const SetObjFxn<unsigned> set_thread_arena{"thread.arena"}; // unsigned rw
const DoObjFxn do_thread_tcache_flush{"thread.tcache.flush"};

const GetObjFxn<bool> config_thp{"config.thp"};
const GetObjFxn<std::string> config_malloc_conf{"config.malloc_conf"};

const GetObjFxn<std::string> thread_prof_name{"thread.prof.name"};
const SetObjFxn<std::string> set_thread_prof_name{"thread.prof.name"};

const GetObjFxn<bool> thread_prof_active{"thread.prof.active"};
const SetObjFxn<bool> set_thread_prof_active{"thread.prof.active"};

int const proc_arena = 0; // default arena for jemalloc

#if !HAVE_LIBJEMALLOC
int const proc_arena_nodump = 0; // default arena for jemalloc
#else
namespace
{
  chunk_alloc_t *s_origAllocHook = nullptr; // safe pre-main
}

int const proc_arena_nodump = []() {
  auto origArena = jemallctl::thread_arena();

  int n = jemallctl::do_arenas_extend();
  jemallctl::set_thread_arena(n);

  chunk_hooks_t origHooks = jemallctl::thread_arena_hooks();
  s_origAllocHook         = origHooks.alloc;

  origHooks.alloc = [](void *old, size_t len, size_t aligned, bool *zero, bool *commit, unsigned arena) {
    void *r = (*s_origAllocHook)(old, len, aligned, zero, commit, arena);

    if (r) {
      madvise(r, aligned_spacing(len, aligned), MADV_DONTDUMP);
    }

    return r;
  };

  jemallctl::set_thread_arena_hooks(origHooks);
  jemallctl::set_thread_arena(origArena); // default again
  return n;
}();
#endif

} // namespace jemallctl
