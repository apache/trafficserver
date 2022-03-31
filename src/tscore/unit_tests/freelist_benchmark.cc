/** @file

  Micro Benchmark tool for global freelist - requires Catch2 v2.9.0+

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

#define CATCH_CONFIG_ENABLE_BENCHMARKING
#define CATCH_CONFIG_RUNNER

#include "catch.hpp"

#include "tscore/ink_hw.h"
#include "tscore/ink_thread.h"
#include "tscore/ink_memory.h"
#include "tscore/ink_queue.h"
#include "tscore/hugepages.h"

#include <iostream>

#if TS_USE_HWLOC
#include <hwloc.h>
#endif

namespace
{
InkFreeList *flist = nullptr;

// Args
int nloop                 = 1000000;
int nthreads              = 1;
int affinity              = 0;
int thread_assiging_order = 0;
bool debug_enabled        = false;

#if TS_USE_HWLOC
hwloc_obj_type_t
thread_affinity()
{
  hwloc_obj_type_t obj_type = HWLOC_OBJ_MACHINE;
  char const *obj_name      = nullptr;

  switch (affinity) {
  case 3: {
    // assign threads to real cores
    obj_type = HWLOC_OBJ_CORE;
    obj_name = "Core";
    break;
  }
  case 1: {
    // assign threads to NUMA nodes (often 1:1 with sockets)
    obj_type = HWLOC_OBJ_NODE;
    obj_name = "NUMA Node";
    if (hwloc_get_nbobjs_by_type(ink_get_topology(), obj_type) > 0) {
      break;
    }
    [[fallthrough]];
  }
  case 2: {
    // assign threads to sockets
    obj_type = HWLOC_OBJ_SOCKET;
    obj_name = "Socket";
    break;
  }
  case 4: {
    // assign threads to logical processing units
#if HAVE_HWLOC_OBJ_PU
    // Older versions of libhwloc (eg. Ubuntu 10.04) don't have HWLOC_OBJ_PU.
    obj_type = HWLOC_OBJ_PU;
    obj_name = "Logical Processor";
    break;
#endif // HAVE_HWLOC_OBJ_PU
    [[fallthrough]];
  }
  default: // assign threads to the machine as a whole (a level below SYSTEM)
    obj_type = HWLOC_OBJ_MACHINE;
    obj_name = "Machine";
  }

  if (debug_enabled) {
    std::cout << "thread affinity type = " << obj_name << " (" << affinity << ")" << std::endl;
  }

  return obj_type;
}
#endif // TS_USE_HWLOC

void *
test_case_1(void *d)
{
  int id;
  void *m1;

  id = (intptr_t)d;

  for (int i = 0; i < nloop; ++i) {
    m1 = ink_freelist_new(flist);

    memset(m1, id, 64);

    ink_freelist_free(flist, m1);
  }

  return nullptr;
}

void
setup_test_case_1(const int64_t n)
{
  ink_thread list[n];

#if TS_USE_HWLOC
  // ThreadAffinityInitializer::set_affinity mimics
  const hwloc_obj_type_t obj_type = thread_affinity();
  const int obj_count             = hwloc_get_nbobjs_by_type(ink_get_topology(), obj_type);

  assert(obj_count > 0);

  for (int i = 0; i < n; i++) {
    ink_thread_create(&list[i], test_case_1, (void *)(static_cast<intptr_t>(i)), 0, 0, nullptr);

    int dst = i;
    if (thread_assiging_order == 1) {
      // Assign threads one side of siblings first
      dst = i * 2;
      if (dst >= obj_count) {
        dst = (i * 2 - obj_count) + 1;
      }
    }

    hwloc_obj_t obj = hwloc_get_obj_by_type(ink_get_topology(), obj_type, dst % obj_count);
    hwloc_set_thread_cpubind(ink_get_topology(), list[i], obj->cpuset, HWLOC_CPUBIND_STRICT);

    if (debug_enabled) {
      int cpu_mask_len = hwloc_bitmap_snprintf(nullptr, 0, obj->cpuset) + 1;
      char *cpu_mask   = static_cast<char *>(alloca(cpu_mask_len));
      hwloc_bitmap_snprintf(cpu_mask, cpu_mask_len, obj->cpuset);

      std::cout << "tid=" << list[i] << " obj->logical_index=" << obj->logical_index << " cpu_mask=" << cpu_mask << std::endl;
    }
  }
#else
  for (int i = 0; i < n; i++) {
    ink_thread_create(&list[i], test_case_1, (void *)((intptr_t)i), 0, 0, nullptr);
  }
#endif

  for (int i = 0; i < n; i++) {
    ink_thread_join(list[i]);
  }
}

TEST_CASE("simple new and free", "")
{
  flist = ink_freelist_create("woof", 64, 256, 8);

  // go 100 times in default (--benchmark-samples)
  char name[16];
  snprintf(name, sizeof(name), "nthreads = %d", nthreads);
  BENCHMARK(name) { return setup_test_case_1(nthreads); };
}
} // namespace

int
main(int argc, char *argv[])
{
  Catch::Session session;

  using namespace Catch::clara;

  bool opt_enable_hugepage = false;

  auto cli = session.cli() |
             Opt(affinity, "type")["--ts-affinity"]("thread affinity type [0-4]\n"
                                                    "0 = HWLOC_OBJ_MACHINE (default)\n"
                                                    "1 = HWLOC_OBJ_NODE\n"
                                                    "2 = HWLOC_OBJ_SOCKET\n"
                                                    "3 = HWLOC_OBJ_CORE\n"
                                                    "4 = HWLOC_OBJ_PU") |
             Opt(nloop, "n")["--ts-nloop"]("number of loop\n"
                                           "(default: 1000000)") |
             Opt(nthreads, "n")["--ts-nthreads"]("number of threads\n"
                                                 "(default: 1)") |
             Opt(opt_enable_hugepage, "yes|no")["--ts-hugepage"]("enable hugepage\n"
                                                                 "(default: no)") |
             Opt(thread_assiging_order, "n")["--ts-thread-order"]("thread assiging order [0-1]\n"
                                                                  "0: use both of sibling of hyper-thread first (default)\n"
                                                                  "1: use a side of sibling of hyper-thread first") |
             Opt(debug_enabled, "yes|no")["--ts-debug"]("enable debuge mode\n");

  session.cli(cli);

  int returnCode = session.applyCommandLine(argc, argv);
  if (returnCode != 0) {
    return returnCode;
  }

  if (debug_enabled) {
    std::cout << "nloop = " << nloop << std::endl;

    if (opt_enable_hugepage) {
      std::cout << "hugepage enabled";
#ifdef MAP_HUGETLB
      ats_hugepage_init(true);
      std::cout << " ats_pagesize=" << ats_pagesize();
      std::cout << " ats_hugepage_size=" << ats_hugepage_size();
      std::cout << std::endl;
#else
      std::cout << "MAP_HUGETLB not defined" << std::endl;
#endif
    }
  }

  return session.run();
}
