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

#include <cstdlib>
#include <cstring>
#include "tscore/ink_thread.h"
#include "tscore/ink_queue.h"

#define NTHREADS 64
InkFreeList *flist = nullptr;

void *
test(void *d)
{
  int id;
  void *m1, *m2, *m3;

  id = (intptr_t)d;

  time_t start = time(nullptr);
  int count    = 0;
  for (;;) {
    m1 = ink_freelist_new(flist);
    m2 = ink_freelist_new(flist);
    m3 = ink_freelist_new(flist);

    if ((m1 == m2) || (m1 == m3) || (m2 == m3)) {
      printf("0x%08" PRIx64 "   0x%08" PRIx64 "   0x%08" PRIx64 "\n", static_cast<uint64_t>((uintptr_t)m1),
             static_cast<uint64_t>((uintptr_t)m2), static_cast<uint64_t>((uintptr_t)m3));
      exit(1);
    }

    memset(m1, id, 64);
    memset(m2, id, 64);
    memset(m3, id, 64);

    ink_freelist_free(flist, m1);
    ink_freelist_free(flist, m2);
    ink_freelist_free(flist, m3);

    // break out of the test if we have run more then 60 seconds
    if (++count % 1000 == 0 && (start + 60) < time(nullptr)) {
      return nullptr;
    }
  }
}

int
main(int /* argc ATS_UNUSED */, char * /*argv ATS_UNUSED */ [])
{
  int i;

  flist = ink_freelist_create("woof", 64, 256, 8);

  for (i = 0; i < NTHREADS; i++) {
    fprintf(stderr, "Create thread %d\n", i);
    ink_thread_create(nullptr, test, (void *)(static_cast<intptr_t>(i)), 0, 0, nullptr);
  }

  test((void *)NTHREADS);

  return 0;
}
