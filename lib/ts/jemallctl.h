/** @file

  Memory allocation routines for libts.

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

#ifndef _JEMALLCTL_H
#define _JEMALLCTL_H

#include "ts/ink_platform.h"
#include "ts/ink_memory.h"

#include <vector>

#if !HAVE_LIBJEMALLOC
struct chunk_hooks_t {
};
#endif

namespace jemallctl
{
extern int const proc_arena;
extern int const proc_arena_nodump;

using objpath_t = std::vector<size_t>;

struct ObjBase {
  ObjBase(const char *name);

protected:
  const objpath_t _oid;
};

template <typename T_VALUE, size_t N_DIFF = 0> struct GetObjFxn : public ObjBase {
  using ObjBase::ObjBase;
  auto operator()(void) const -> T_VALUE;
};

template <typename T_VALUE, size_t N_DIFF = 0> struct SetObjFxn : public ObjBase {
  using ObjBase::ObjBase;
  auto operator()(const T_VALUE &) const -> int;
};

template <> struct GetObjFxn<void, 0> : public ObjBase {
  using ObjBase::ObjBase;
  int operator()(void) const;
};

using DoObjFxn = GetObjFxn<void, 0>;

chunk_hooks_t const &get_hugepage_hooks();
chunk_hooks_t const &get_hugepage_nodump_hooks();

extern const GetObjFxn<chunk_hooks_t> thread_arena_hooks;
extern const SetObjFxn<chunk_hooks_t> set_thread_arena_hooks;

// request-or-sense new values in statistics
extern const GetObjFxn<uint64_t> epoch;

// request separated page sets for each NUMA node (when created)
extern const GetObjFxn<unsigned> do_arenas_extend;

// assigned arena for local thread
extern const GetObjFxn<unsigned> thread_arena;
extern const SetObjFxn<unsigned> set_thread_arena;
extern const DoObjFxn do_thread_tcache_flush;

// from the build-time config
extern const GetObjFxn<bool> config_thp;
extern const GetObjFxn<std::string> config_malloc_conf;

// for profiling only
extern const GetObjFxn<std::string> thread_prof_name;
extern const SetObjFxn<std::string> set_thread_prof_name;
extern const GetObjFxn<bool> thread_prof_active;
extern const SetObjFxn<bool> set_thread_prof_active;
}

#endif // _JEMALLCTL_H
