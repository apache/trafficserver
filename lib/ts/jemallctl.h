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
#include <atomic>

namespace jemallctl
{
using objpath_t = std::vector<size_t>;

struct ObjBase {
  ObjBase(const char *name);

protected:
  const objpath_t _oid;
  const char *_name;
};

//
// define API per jemallctl optional parameter
//
template <typename T_VALUE, size_t N_DIFF = 0> struct GetObjFxn : public ObjBase {
  using ObjBase::ObjBase;
  T_VALUE operator()(void) const;
};

template <typename T_VALUE, size_t N_DIFF = 0> struct SetObjFxn : public ObjBase {
  using ObjBase::ObjBase;
  int operator()(const T_VALUE &) const;
};

template <> struct SetObjFxn<bool, 0> : public ObjBase {
  using ObjBase::ObjBase;
  int operator()(void) const;
};

template <> struct SetObjFxn<bool, 1> : public ObjBase {
  using ObjBase::ObjBase;
  int operator()(void) const;
};

template <> struct GetObjFxn<void, 0> : public ObjBase {
  using ObjBase::ObjBase;
  int operator()(void) const;
};

#if !HAVE_LIBJEMALLOC
inline ObjBase::ObjBase(const char *name) 
   : _name(name)
{
}

template <typename T_VALUE, size_t N_DIFF>
inline T_VALUE
GetObjFxn<T_VALUE, N_DIFF>::operator()(void) const
{
  return -1;
}

template <typename T_VALUE, size_t N_DIFF>
inline int
SetObjFxn<T_VALUE, N_DIFF>::operator()(const T_VALUE &v) const
{
  return -1;
}

template <>
inline int
SetObjFxn<unsigned, 0>::operator()(const unsigned &v) const
{
  return -1;
}

inline int
GetObjFxn<void, 0>::operator()(void) const
{
  return -1;
}

inline int
SetObjFxn<bool, 0>::operator()(void) const
{
  return -1;
}

inline int
SetObjFxn<bool, 1>::operator()(void) const
{
  return -1;
}
#endif

int create_global_nodump_arena();

using EnableObjFxn  = SetObjFxn<bool, 1>;
using DisableObjFxn = SetObjFxn<bool, 0>;
using DoObjFxn      = GetObjFxn<void, 0>;

#if HAVE_LIBJEMALLOC
extern const GetObjFxn<chunk_hooks_t> thread_arena_hooks;
extern const SetObjFxn<chunk_hooks_t> set_thread_arena_hooks;
#endif

// request-or-sense new values in statistics
extern const GetObjFxn<uint64_t> epoch;

// request separated page sets for each NUMA node (when created)
extern const GetObjFxn<unsigned> do_arenas_extend;

// assigned arena for local thread
extern const GetObjFxn<unsigned> thread_arena;
extern const SetObjFxn<unsigned> set_thread_arena;
extern const DoObjFxn do_thread_tcache_flush;
extern const GetObjFxn<uint64_t *> thread_allocatedp;
extern const GetObjFxn<uint64_t *> thread_deallocatedp;

// from the build-time config
extern const GetObjFxn<bool> config_thp;
extern const GetObjFxn<std::string> config_malloc_conf;

// for profiling only
extern const GetObjFxn<bool> prof_active;
extern const EnableObjFxn enable_prof_active;
extern const DisableObjFxn disable_prof_active;

extern const GetObjFxn<std::string> thread_prof_name;
extern const SetObjFxn<std::string> set_thread_prof_name;

extern const GetObjFxn<bool> thread_prof_active;
extern const EnableObjFxn enable_thread_prof_active;
extern const DisableObjFxn disable_thread_prof_active;

extern const GetObjFxn<uint64_t> stats_active;
extern const GetObjFxn<std::atomic_ulong *> stats_cactive;
extern const GetObjFxn<uint64_t> stats_allocated;

extern const GetObjFxn<bool *> arenas_initialized;
extern const GetObjFxn<unsigned> arenas_narenas;
}

#endif // _JEMALLCTL_H
