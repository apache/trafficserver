/** @file

  A brief file description

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

/****************************************************************************

  NUMADebug.h

  This file contains code for debugging NUMA support.

 ****************************************************************************/

#pragma once

#include "tscore/ink_config.h"

#if TS_ENABLE_NUMA_DEBUG

#include "tscore/Diags.h"
#include <atomic>
#include <cstdint>

class NUMA_CHECK_set_unset
{
public:
  NUMA_CHECK_set_unset(bool *b) : m_b(b) { *m_b = true; }
  ~NUMA_CHECK_set_unset() { *m_b = false; }

private:
  bool *m_b;
};

// Sets a number reported in the log messages. If set to negative value, most checks will be disabled for the thread.
void NUMA_CHECK_set_thread_kind(int i);
// Sets desired node to check against (for pinned threads)
void NUMA_CHECK_set_desired_node(int i);

#define NUMA_CHECK_MAX_NUMA_NODES 8
// Returns -1 if can not be determined
int NUMA_CHECK_get_node_of_memory(void *ptr, bool verbose = false);
class CombinedNUMACheck
{
  std::atomic<uint64_t> node_hit_count[NUMA_CHECK_MAX_NUMA_NODES]{};
  std::atomic<uint64_t> node_mismatch_count[NUMA_CHECK_MAX_NUMA_NODES]{};
  std::atomic<uint64_t> mismatch_count{};
  std::atomic<uint64_t> fail_count{};
  std::atomic<uint64_t> prints_done{};
  std::atomic<uint64_t> lcg{1}; // used to randomize offset
public:
  static constexpr unsigned int invalid_node = (unsigned int)-1;
  void check(const void *address, const size_t size, const char *file, int line, const char *func, const char *variable_name);
};

#define NUMA_CHECK_STRINGIZE(x)  NUMA_CHECK_STRINGIZE2(x)
#define NUMA_CHECK_STRINGIZE2(x) #x
#define NUMA_CHECK_LINE_STRING   NUMA_CHECK_STRINGIZE(__LINE__)

#define NUMA_CHECK(var, size)                             \
  do {                                                    \
    static thread_local bool recurse = false;             \
    if (!recurse) {                                       \
      NUMA_CHECK_set_unset lock(&recurse);                \
      [](const void *ptr, size_t sz, const char *func) {  \
        static CombinedNUMACheck c;                       \
        c.check(ptr, sz, __FILE__, __LINE__, func, #var); \
      }(var, size, __func__);                             \
    }                                                     \
  } while (false)

#define NUMA_CHECK_SET_THREAD_KIND(kind)  NUMA_CHECK_set_thread_kind(kind)
#define NUMA_CHECK_SET_DESIRED_NODE(node) NUMA_CHECK_set_desired_node(node)

#else

#define NUMA_CHECK(...)
#define NUMA_CHECK_SET_THREAD_KIND(...)
#define NUMA_CHECK_SET_DESIRED_NODE(...)

#endif
