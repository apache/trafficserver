/** @file

   Catch-based unit tests for HdrHeap

   @section license License

   Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
   See the NOTICE file distributed with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License.  You may obtain a
   copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.
 */

#include "catch.hpp"

#include "HdrHeap.h"
#include "URL.h"

/**
  This test is designed to test numerous pieces of the HdrHeaps including allocations,
  demotion of rw heaps to ronly heaps, and finally the coalesce and evacuate behaviours.
 */
TEST_CASE("HdrHeap", "[proxy][hdrheap]")
{
  // The amount of space we will need to overflow the StrHdrHeap is HdrStrHeap::DEFAULT_SIZE - sizeof(HdrStrHeap)
  size_t next_rw_heap_size           = HdrStrHeap::DEFAULT_SIZE;
  size_t next_required_overflow_size = next_rw_heap_size - sizeof(HdrStrHeap);
  char buf[next_required_overflow_size];
  for (unsigned int i = 0; i < sizeof(buf); ++i) {
    buf[i] = ('a' + (i % 26));
  }

  HdrHeap *heap = new_HdrHeap();
  URLImpl *url  = url_create(heap);

  // Checking that we have no rw heap
  CHECK(heap->m_read_write_heap.get() == nullptr);
  url->set_path(heap, buf, next_required_overflow_size, true);

  // Checking that we've completely consumed the rw heap
  CHECK(heap->m_read_write_heap->m_free_size == 0);

  // Checking ronly_heaps are empty
  for (unsigned i = 0; i < HDR_BUF_RONLY_HEAPS; ++i) {
    CHECK(heap->m_ronly_heap[i].m_heap_start == nullptr);
  }

  // Now we have no ronly heaps in use and a completely full rwheap, so we will test that
  // we demote to ronly heaps HDR_BUF_RONLY_HEAPS times.
  for (unsigned ronly_heap = 0; ronly_heap < HDR_BUF_RONLY_HEAPS; ++ronly_heap) {
    next_rw_heap_size           = 2 * heap->m_read_write_heap->m_heap_size;
    next_required_overflow_size = next_rw_heap_size - sizeof(HdrStrHeap);
    char buf2[next_required_overflow_size];
    for (unsigned int i = 0; i < sizeof(buf2); ++i) {
      buf2[i] = ('a' + (i % 26));
    }

    URLImpl *url2 = url_create(heap);
    url2->set_path(heap, buf2, next_required_overflow_size, true);

    // Checking the current rw heap is next_rw_heap_size bytes
    CHECK(heap->m_read_write_heap->m_heap_size == (uint32_t)next_rw_heap_size);
    // Checking that we've completely consumed the rw heap
    CHECK(heap->m_read_write_heap->m_free_size == 0);
    // Checking that we properly demoted the previous rw heap
    CHECK(heap->m_ronly_heap[ronly_heap].m_heap_start != nullptr);

    for (unsigned i = ronly_heap + 1; i < HDR_BUF_RONLY_HEAPS; ++i) {
      // Checking ronly_heap[i] is empty
      CHECK(heap->m_ronly_heap[i].m_heap_start == nullptr);
    }
  }

  // We will rerun these checks after we introduce a non-copied string to make sure we didn't already coalesce
  for (unsigned i = 0; i < HDR_BUF_RONLY_HEAPS; ++i) {
    // Pre non-copied string: Checking ronly_heaps[i] is NOT empty
    CHECK(heap->m_ronly_heap[i].m_heap_start != nullptr);
  }

  // Now if we add a url object that contains only non-copied strings it shouldn't affect the size of the rwheap
  // since it doesn't require allocating any storage on this heap.
  char buf3[next_required_overflow_size];
  for (unsigned int i = 0; i < sizeof(buf3); ++i) {
    buf3[i] = ('a' + (i % 26));
  }

  URLImpl *aliased_str_url = url_create(heap);
  aliased_str_url->set_path(heap, buf3, next_required_overflow_size, false); // don't copy this string
  // Checking that the aliased string shows having proper length
  CHECK(aliased_str_url->m_len_path == next_required_overflow_size);
  // Checking that the aliased string is correctly pointing at buf
  CHECK(aliased_str_url->m_ptr_path == &buf3[0]);

  // Post non-copied string: Checking ronly_heaps are NOT empty
  for (unsigned i = 0; i < HDR_BUF_RONLY_HEAPS; ++i) {
    CHECK(heap->m_ronly_heap[i].m_heap_start != nullptr);
  }
  // Checking that we've completely consumed the rw heap
  CHECK(heap->m_read_write_heap->m_free_size == 0);
  // Checking that we dont have any chained heaps
  CHECK(heap->m_next == nullptr);

  // Now at this point we have a completely full rw_heap and no ronly heap slots, so any allocation would have to result
  // in a coalesce, and to validate that we don't reintroduce TS-2766 we have an aliased string, so when it tries to
  // coalesce it used to sum up the size of the ronly heaps and the rw heap which is incorrect because we never
  // copied the above string onto the heap. The new behaviour fixed in TS-2766 will make sure that this non copied
  // string is accounted for, in the old implementation it would result in an allocation failure.

  char *str = heap->allocate_str(1); // this will force a coalesce.
  // Checking that 1 byte allocated string is not nullptr
  CHECK(str != nullptr);

  // Now we need to validate that aliased_str_url has a path that isn't nullptr, if it's nullptr then the
  // coalesce is broken and didn't properly determine the size, if it's not nullptr then everything worked as expected.

  // Checking that the aliased string shows having proper length
  CHECK(aliased_str_url->m_len_path == next_required_overflow_size);
  // Checking that the aliased string was properly moved during coalesce and evacuation
  CHECK(aliased_str_url->m_ptr_path != nullptr);
  // Checking that the aliased string was properly moved during coalesce and evacuation (not pointing at buf3)
  CHECK(aliased_str_url->m_ptr_path != &buf3[0]);

  // Clean up
  heap->destroy();
}
