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

#include "tscore/ink_config.h"
#include <cassert>

#include "tscore/ink_atomic.h"
#include "tscore/ink_queue.h"

/*
 * This file was added during the debugging of Bug 50475.
 * It was found that a race condition was introduced on the sparc architecture
 * when the ink_queue.c file was moved over to ink_queue.cc. Debugging this
 * problem resulted in the discovery that gcc was spitting out the
 * "ldd" (load double) instruction for loading of the 64 bit field "data"
 * while CC was spitting out two "ld" (load) instructions. The old code
 * was calling item.data = head.data on Sparcs and not putting any restriction
 * on the order of loading of the fields.
 *
 * This is a problem on the Sparcs because the "pointer" field was being loaded
 * before the "version" field. This can result in a very subtle race condition
 * which subverts the addition of the "version" field.
 *
 * Take this scenario
 * item.ptr = head.ptr
 * (call to ink_freelist_new )
 * next.ptr = *(item.ptr) <---- Error
 * (call to ink_freelist_free )
 * item.version = head.version
 * next.version = item.version ++;
 * cas64(head, item, next)

 * Note, that the cas64 call will be successful and the next.ptr will probably
 * be a pointer into the vtable entry. The next alloc will result in a write into
 * the vtable area.
 *
 * The fix for this problem is to read the version before reading the pointer
 * on 32 bit architectures (currently everything other than alphas). This is
 * done using the following function. This file only contains one function
 * to make looking at the assembly code simple.
 *
 * If you ever change the compiler or the compiler options to this file, make
 * sure you look at the assembly generated to see if the version is read first.
 * Also, make sure that you run the test_freelist microbenchmark for at least
 * 24 hours on a dual processor box.
 */

void
ink_queue_load_64(void *dst, void *src)
{
#if (defined(__i386__) || defined(__arm__) || defined(__mips__)) && (SIZEOF_VOIDP == 4)
  int32_t src_version = (*(head_p *)src).s.version;
  void *src_pointer   = (*(head_p *)src).s.pointer;

  (*(head_p *)dst).s.version = src_version;
  (*(head_p *)dst).s.pointer = src_pointer;
#else
  *static_cast<void **>(dst) = *static_cast<void **>(src);
#endif
}
