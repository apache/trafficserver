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

  NUMADebug.cc

  This file contains code for debugging NUMA support.

 ****************************************************************************/

#include "tscore/NUMADebug.h"
#include <numaif.h>

thread_local unsigned int last_node                = CombinedNUMACheck::invalid_node;
thread_local uint64_t     thread_numa_change_count = 0;
thread_local int          thread_kind              = 0;
thread_local int          desired_node             = -1;

void
NUMA_CHECK_set_thread_kind(int i)
{
  thread_kind = i;
}

void
NUMA_CHECK_set_desired_node(int i)
{
  desired_node = i;
}

static constexpr intptr_t PAGE_SIZE = 4096;

static void *
align_pointer_to_page(const void *ptr)
{
  // Align to the page size (move_page requires that)

  return reinterpret_cast<void *>(reinterpret_cast<intptr_t>(ptr) & (~(PAGE_SIZE - 1)));
}

int
NUMA_CHECK_get_node_of_memory(void *ptr, bool verbose)
{
  if (!ptr)
    return -1;
  void *address  = align_pointer_to_page(ptr);
  int   mem_node = -1;
  auto  result   = move_pages(0, 1, &address, NULL, &mem_node, 0);
  if (result == 0) {
    if (mem_node >= 0) {
      return mem_node;
    } else { // negative error here if we can't determine it
      if (verbose)
        ink_notice("Negative status %d after move pages", mem_node);
    }
  } else {
    int err = errno;
    ink_notice("Error calling move_pages, errno = %d", err);
  }
  return -1;
}

static std::atomic<uint64_t> stack_checks{0};
static std::atomic<uint64_t> stack_fails{0};
static std::atomic<uint64_t> stack_prints{0};

unsigned int
getcpu_and_check()
{
  unsigned int node = 0;
  int          err  = getcpu(nullptr, &node);
  if (err != 0) {
    ink_error("getcpu failed");
  }
  bool got_thread_name = false;
  char thread_name[32];

  if (node != last_node) {
    if (last_node != CombinedNUMACheck::invalid_node) {
      thread_numa_change_count++;
      ink_get_thread_name(thread_name, sizeof(thread_name));
      got_thread_name = true;
      ink_notice("Thread %s hopped from numa%d to numa%i, thread kind: %d", thread_name, last_node, node, thread_kind);
    }
    last_node = node;
  }
  if (desired_node >= 0 && (unsigned int)desired_node != node) {
    if (!got_thread_name) {
      ink_get_thread_name(thread_name, sizeof(thread_name));
      got_thread_name = true;
    }
    ink_notice("Thread %s is supposed to be on numa%d but is on numa%i instead, thread kind: %d", thread_name, desired_node, node,
               thread_kind);
  }
  // Verify that stack is on the same node as the thread, but only for non-main thread
  if (desired_node >= 0) {
    void *stack_ptr = align_pointer_to_page(&node);
    int   mem_node  = 0;
    auto  result    = move_pages(0, 1, &stack_ptr, NULL, &mem_node, 0);
    if (result == 0) {
      if (mem_node >= 0) {
        uint64_t check_cnt = stack_checks++;
        if ((unsigned int)mem_node != node) {
          stack_fails++;
        }
        uint64_t fail_cnt = stack_fails;
        if (check_cnt >= (1ULL << stack_prints)) {
          stack_prints++;
          if (!got_thread_name) {
            ink_get_thread_name(thread_name, sizeof(thread_name));
            got_thread_name = true;
          }
          double mismatch_percent = 100.0 * (double)fail_cnt / (double)check_cnt;
          ink_notice("Thread %s stack mismatch rate=%04.1f%% total hits=%" PRIu64 " mismatch=%" PRIu64 " ", thread_name,
                     mismatch_percent, check_cnt, fail_cnt);
        }
      } else {
        ink_notice("Error checking stack numa node, returned %d", mem_node);
      }
    } else {
      int err = errno;
      ink_notice("Error checking stack numa node, errno=%d", err);
    }
  }

  return node;
}

void
CombinedNUMACheck::check(const void *in_address, size_t size, const char *file, int line, const char *func,
                         const char *variable_name)
{
  // NULLs do not count
  if (!in_address || thread_kind < 0)
    return;
  unsigned int node = getcpu_and_check();

  int status[1];
  status[0] = 0;

  uint64_t size_in_pages = size / PAGE_SIZE;
  if (size_in_pages > 0) {
    size_t offset = (size_in_pages * (lcg >> 32)) >> 32;
    // should not happen but don't want to debug
    if (offset > size_in_pages)
      offset %= size_in_pages;
    in_address = reinterpret_cast<void *>(reinterpret_cast<intptr_t>(in_address) + offset * PAGE_SIZE);
    lcg        = lcg * 6364136223846793005ULL + 1;
  }

  // Align to the page size (move_page requires that)
  void *address = align_pointer_to_page(in_address);

  auto result = move_pages(0, 1, &address, NULL, status, 0);
  if (result == 0) {
    int mem_node = status[0];
    if (mem_node >= 0 && mem_node < NUMA_CHECK_MAX_NUMA_NODES) {
      node_hit_count[mem_node]++;
      if ((unsigned int)mem_node != node) {
        mismatch_count++;
        node_mismatch_count[mem_node]++;
      }
    } else {
      fail_count++;
      // Ignore to avoid spam
      // msg.message(DL_Warning, loc, "Negative status after calling move_pages, value = %d", mem_node);
    }
  } else {
    int err = errno;
    ink_notice("NUMA check at %s:%d (%s, %s): Error calling move_pages, errno = %d", file, line, func, variable_name, err);
  }

  // May be slightly inaccurate due to simultaneous updating by multiple threads.
  uint64_t combined_hit_count = 0;
  uint64_t l_mismatch_count   = mismatch_count;

  int   sz = NUMA_CHECK_MAX_NUMA_NODES * 21;
  char  hit_count_string[NUMA_CHECK_MAX_NUMA_NODES * 50];
  char *pos = hit_count_string;
  for (int i = 0; i < NUMA_CHECK_MAX_NUMA_NODES; ++i) {
    uint64_t nhc = node_hit_count[i];
    // Cut stats short
    if (nhc == 0)
      break;
    double node_fail_rate = -1;
    if (nhc > 0) {
      node_fail_rate = (double)node_mismatch_count[i] / (double)node_hit_count[i];
    }
    combined_hit_count += nhc;
    if (sz > 0) {
      int cnt  = snprintf(pos, (size_t)sz, "%" PRIu64 " ", nhc);
      pos     += cnt;
      sz      -= cnt;

      cnt  = snprintf(pos, (size_t)sz, "%0.4f, ", node_fail_rate);
      pos += cnt;
      sz  -= cnt;
    }
  }
  // Doubling of print intervals (to prevent excessive log spamming)
  if (combined_hit_count >= (1ULL << prints_done)) {
    prints_done++;
    double   mismatch_percent = 100.0 * (double)l_mismatch_count / (double)combined_hit_count;
    uint64_t l_fail_count     = fail_count;
    ink_notice("NUMA check at %s:%d (%s, %s): mismatch_rate=%04.1f%% fails=%" PRIu64 " hits=%" PRIu64 " mismatch=%" PRIu64
               " thread numa switches=%" PRIu64 " hits=[%s]",
               file, line, func, variable_name, mismatch_percent, l_fail_count, combined_hit_count, l_mismatch_count,
               thread_numa_change_count, hit_count_string);
  }
}
