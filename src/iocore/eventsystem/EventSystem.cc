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

   EventSystem.cc --


****************************************************************************/

#include "P_EventSystem.h"
#include "tscore/hugepages.h"
#include "records/RecCore.h"

void
ink_event_system_init(ts::ModuleVersion v)
{
  ink_release_assert(v.check(EVENT_SYSTEM_MODULE_INTERNAL_VERSION));
  int iobuffer_advice = 0;

  // For backwards compatibility make sure to allow thread_freelist_size
  // This needs to change in 6.0
  REC_EstablishStaticConfigInt32(thread_freelist_high_watermark, "proxy.config.allocator.thread_freelist_size");

  REC_EstablishStaticConfigInt32(thread_freelist_low_watermark, "proxy.config.allocator.thread_freelist_low_watermark");

  int   chunk_sizes[DEFAULT_BUFFER_SIZES] = {0};
  char *chunk_sizes_string                = REC_ConfigReadString("proxy.config.allocator.iobuf_chunk_sizes");
  if (chunk_sizes_string && !parse_buffer_chunk_sizes(chunk_sizes_string, chunk_sizes)) {
    // If we can't parse the string then we can't be sure of the chunk sizes so just exit
    Fatal("Failed to parse proxy.config.allocator.iobuf_chunk_sizes");
  }
  ats_free(chunk_sizes_string);

  bool use_hugepages = ats_hugepage_enabled();

#ifdef MADV_DONTDUMP // This should only exist on Linux 3.4 and higher.
  RecBool dont_dump_enabled = true;
  RecGetRecordBool("proxy.config.allocator.dontdump_iobuffers", &dont_dump_enabled, false);

  if (dont_dump_enabled) {
    iobuffer_advice |= MADV_DONTDUMP;
  }
#endif

  init_buffer_allocators(iobuffer_advice, chunk_sizes, use_hugepages);
}
