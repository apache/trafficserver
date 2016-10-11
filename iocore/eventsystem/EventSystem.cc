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

void
ink_event_system_init(ModuleVersion v)
{
  ink_release_assert(!checkModuleVersion(v, EVENT_SYSTEM_MODULE_VERSION));
  int config_max_iobuffer_size = DEFAULT_MAX_BUFFER_SIZE;

  // For backwards compatability make sure to allow thread_freelist_size
  // This needs to change in 6.0
  REC_EstablishStaticConfigInt32(thread_freelist_high_watermark, "proxy.config.allocator.thread_freelist_size");

  REC_EstablishStaticConfigInt32(thread_freelist_low_watermark, "proxy.config.allocator.thread_freelist_low_watermark");

  REC_ReadConfigInteger(config_max_iobuffer_size, "proxy.config.io.max_buffer_size");

  max_iobuffer_size = buffer_size_to_index(config_max_iobuffer_size, DEFAULT_BUFFER_SIZES - 1);
  if (default_small_iobuffer_size > max_iobuffer_size) {
    default_small_iobuffer_size = max_iobuffer_size;
  }
  if (default_large_iobuffer_size > max_iobuffer_size) {
    default_large_iobuffer_size = max_iobuffer_size;
  }

#ifdef MADV_DONTDUMP // This should only exist on Linux 3.4 and higher.
  RecBool dont_dump_enabled = true;
  RecGetRecordBool("proxy.config.allocator.dontdump_iobuffers", &dont_dump_enabled, false);

  if (dont_dump_enabled) {
    iobuffer_advice |= MADV_DONTDUMP;
  }
#endif

  init_buffer_allocators();
}
