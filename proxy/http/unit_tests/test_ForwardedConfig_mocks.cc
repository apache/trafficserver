/** @file

  Mocks for unit test of ForwardedConfig.cc

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

#include <cstdlib>
#include <iostream>

#include <I_EventSystem.h>
#include <I_Thread.h>

void
_ink_assert(const char *expression, const char *file, int line)
{
  std::cerr << "fatal error: ink_assert: file: " << file << " line: " << line << " expression: " << expression << std::endl;

  std::exit(1);
}

namespace
{
void
stub(const char *file, int line)
{
  std::cerr << "fatal error: call to link stub: file: " << file << " line: " << line << std::endl;

  std::exit(1);
}
} // namespace

#define STUB stub(__FILE__, __LINE__);

inkcoreapi void
ink_freelist_init(InkFreeList **fl, const char *name, uint32_t type_size, uint32_t chunk_size, uint32_t alignment)
{
}
inkcoreapi void
ink_freelist_free(InkFreeList *f, void *item){STUB} inkcoreapi
  void ink_freelist_free_bulk(InkFreeList *f, void *head, void *tail, size_t num_item)
{
  STUB
}
void ink_mutex_destroy(pthread_mutex_t *){STUB} inkcoreapi ClassAllocator<ProxyMutex> mutexAllocator("ARGH");
inkcoreapi ink_thread_key Thread::thread_data_key;
int res_track_memory;
void ResourceTracker::increment(const char *, int64_t){STUB} inkcoreapi Allocator ioBufAllocator[DEFAULT_BUFFER_SIZES];
void
ats_free(void *)
{
  STUB
}
int thread_freelist_high_watermark;
int thread_freelist_low_watermark;
inkcoreapi ClassAllocator<IOBufferBlock> ioBlockAllocator("ARGH");
inkcoreapi ClassAllocator<IOBufferData> ioDataAllocator("ARGH");
IOBufferBlock::IOBufferBlock() {}

void
IOBufferBlock::free()
{
}

void
IOBufferData::free()
{
}
