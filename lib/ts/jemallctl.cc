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

#include "ts/ink_platform.h"
#include "ts/ink_memory.h"

// compile this regardless of global use
#ifndef HAVE_LIBJEMALLOC
#define HAVE_LIBJEMALLOC   1
#endif 
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

#include <sys/syscall.h> /* For SYS_xxx definitions */

namespace jemallctl
{
// internal read/write functions

int mallctl_void(const objpath_t &oid);

template <typename T_VALUE> auto mallctl_get(const objpath_t &oid) -> T_VALUE;

template <typename T_VALUE> auto mallctl_set(const objpath_t &oid, const T_VALUE &v) -> int;

// define object functors (to allow instances below)

objpath_t objpath(const std::string &path);

ObjBase::ObjBase(const char *name) : _oid(objpath(name)), _name(name)
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
  auto r = ::jemallctl::mallctl_set(ObjBase::_oid, v);
  ink_assert(!r);
  return r;
}

template <>
auto
SetObjFxn<unsigned, 0>::operator()(const unsigned &v) const -> int
{
  auto ov = ::jemallctl::mallctl_get<unsigned>(ObjBase::_oid);
  if (ov == v) {
    return 0;
  }

  auto r = ::jemallctl::mallctl_set(ObjBase::_oid, v);
  ink_assert(!r);
  ink_assert(v == ::jemallctl::mallctl_get<unsigned>(ObjBase::_oid));
  Debug("memory", "confirmed tid=%ld %s: %u->%u", syscall(__NR_gettid), _name, ov, v);
  return r;
}

int
GetObjFxn<void, 0>::operator()(void) const
{
  return ::jemallctl::mallctl_void(ObjBase::_oid);
}

int
SetObjFxn<bool, 0>::operator()(void) const
{
  auto v = false;
  auto r = ::jemallctl::mallctl_set(ObjBase::_oid, v);
  ink_assert(!r);
  return r;
}

int
SetObjFxn<bool, 1>::operator()(void) const
{
  auto v = true;
  auto r = ::jemallctl::mallctl_set(ObjBase::_oid, v);
  ink_assert(!r);
  return r;
}

objpath_t
objpath(const std::string &path)
{
  objpath_t oid;

  oid.resize(10); // longer than any oid target// *explicitly resize*
  size_t len = oid.size();
  auto r     = mallctlnametomib(path.c_str(), oid.data(), &len);
  ink_assert(!r);
  oid.resize(len);
  return std::move(oid);
}

/// implementations for ObjBase

auto
mallctl_void(const objpath_t &oid) -> int
{
  auto r = mallctlbymib(oid.data(), oid.size(), nullptr, nullptr, nullptr, 0);
  ink_assert(!r);
  return r;
}

template <typename T_VALUE>
auto
mallctl_set(const objpath_t &oid, const T_VALUE &v) -> int
{
  auto r = mallctlbymib(oid.data(), oid.size(), nullptr, nullptr, const_cast<T_VALUE *>(&v), sizeof(v));
  ink_assert(!r);
  return r;
}

template <typename T_VALUE>
auto
mallctl_get(const objpath_t &oid) -> T_VALUE
{
  T_VALUE v{}; // init to zero if a pod type
  size_t len = sizeof(v);
  auto r     = mallctlbymib(oid.data(), oid.size(), &v, &len, nullptr, 0);
  ink_assert(!r);
  return std::move(v);
}

template <>
auto
mallctl_get<std::string>(const objpath_t &oid) -> std::string
{
  const char *cstr = nullptr;
  size_t len       = sizeof(cstr);
  auto r           = mallctlbymib(oid.data(), oid.size(), &cstr, &len, nullptr, 0);
  ink_assert(!r && cstr);
  std::string v(cstr); // copy out
  return std::move(v);
}

template <>
auto
mallctl_set<std::string>(const objpath_t &oid, const std::string &v) -> int
{
  const char *cstr = v.c_str();
  size_t len       = sizeof(cstr);
  auto r           = mallctlbymib(oid.data(), oid.size(), nullptr, nullptr, &cstr, len); // ptr-to-c-string-ptr
  ink_assert(!r);
  return 0;
}

namespace
{
  std::vector<char> s_bools(256, 0);
}

template <>
auto
mallctl_get<bool *>(const objpath_t &oid) -> bool *
{
  size_t len = arenas_narenas() * sizeof(s_bools[0]);
  ink_assert(len < s_bools.size());
  auto v = s_bools.data();
  auto r = mallctlbymib(oid.data(), oid.size(), v, &len, nullptr, 0);
  ink_assert(!r);
  return std::move(reinterpret_cast<bool *>(v));
}

template <>
auto
mallctl_get<chunk_hooks_t>(const objpath_t &baseOid) -> chunk_hooks_t
{
  objpath_t oid = baseOid;
  oid[1]        = thread_arena();

  chunk_hooks_t v;
  size_t len = sizeof(v);
  auto r     = mallctlbymib(oid.data(), oid.size(), &v, &len, nullptr, 0);
  ink_assert(!r);
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
  auto r = mallctlbymib(oid.data(), oid.size(), nullptr, nullptr, const_cast<chunk_hooks_t *>(&nhooks), sizeof(nhooks));
  ink_assert(!r);
  return 0;
}

namespace
{
  chunk_alloc_t *s_origAllocHook = nullptr; // safe pre-main
}

int
create_global_nodump_arena()
{
  auto origArena = jemallctl::thread_arena();

  // fork from base nodes set (id#0)
  auto newArena = jemallctl::do_arenas_extend();

  jemallctl::set_thread_arena(newArena);

  chunk_hooks_t origHooks = jemallctl::thread_arena_hooks();
  s_origAllocHook         = origHooks.alloc;

  origHooks.alloc = [](void *old, size_t len, size_t aligned, bool *zero, bool *commit, unsigned arena) {
    void *r = (*s_origAllocHook)(old, len, aligned, zero, commit, arena);

    if (r) {
      madvise(r, aligned_size(len, aligned), MADV_DONTDUMP);
    }

    return r;
  };

  jemallctl::set_thread_arena_hooks(origHooks);

  jemallctl::set_thread_arena(origArena); // default again
  return newArena;
}

template struct GetObjFxn<uint64_t>;
template struct GetObjFxn<uint64_t *>;
template struct GetObjFxn<std::atomic_ulong *>;
template struct GetObjFxn<unsigned>;
template struct GetObjFxn<bool>;
template struct GetObjFxn<bool *>;
template struct GetObjFxn<chunk_hooks_t>;
template struct GetObjFxn<std::string>;

template struct SetObjFxn<uint64_t>;
template struct SetObjFxn<unsigned>;
template struct SetObjFxn<bool>;
template struct SetObjFxn<chunk_hooks_t>;
template struct SetObjFxn<std::string>;

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

const GetObjFxn<std::string> config_malloc_conf{"config.malloc_conf"};

const GetObjFxn<std::string> thread_prof_name{"thread.prof.name"};
const SetObjFxn<std::string> set_thread_prof_name{"thread.prof.name"};

// for profiling only
const GetObjFxn<bool> prof_active{"prof.active"};
const EnableObjFxn enable_prof_active{"prof.active"};
const DisableObjFxn disable_prof_active{"prof.active"};

const GetObjFxn<bool> thread_prof_active{"thread.prof.active"};
const EnableObjFxn enable_thread_prof_active{"thread.prof.active"};
const DisableObjFxn disable_thread_prof_active{"thread.prof.active"};

const GetObjFxn<uint64_t *> thread_allocatedp{"thread.allocatedp"};
const GetObjFxn<uint64_t *> thread_deallocatedp{"thread.deallocatedp"};

const GetObjFxn<std::atomic_ulong *> stats_cactive{"stats.cactive"};
const GetObjFxn<uint64_t> stats_active{"stats.active"};
const GetObjFxn<uint64_t> stats_allocated{"stats.allocated"};
const GetObjFxn<bool *> arenas_initialized{"arenas.initialized"};
const GetObjFxn<unsigned> arenas_narenas{"arenas.narenas"};
} // namespace jemallctl
