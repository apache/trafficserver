/** @file

  Queue of Events sorted by the "timeout_at" field impl as binary heap

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

#include "P_EventSystem.h"

PriorityEventQueue::PriorityEventQueue()
{
  last_check_time    = Thread::get_hrtime_updated();
  last_check_buckets = last_check_time / PQ_BUCKET_TIME(0);
}

void
PriorityEventQueue::check_ready(ink_hrtime now, EThread *t)
{
  int i, j, k = 0;
  uint32_t check_buckets = (uint32_t)(now / PQ_BUCKET_TIME(0));
  uint32_t todo_buckets  = check_buckets ^ last_check_buckets;
  last_check_time        = now;
  last_check_buckets     = check_buckets;
  todo_buckets &= ((1 << (N_PQ_LIST - 1)) - 1);
  while (todo_buckets) {
    k++;
    todo_buckets >>= 1;
  }
  for (i = 1; i <= k; i++) {
    Event *e;
    Que(Event, link) q = after[i];
    after[i].clear();
    while ((e = q.dequeue()) != nullptr) {
      if (e->cancelled) {
        e->in_the_priority_queue = 0;
        e->cancelled             = 0;
        EVENT_FREE(e, eventAllocator, t);
      } else {
        ink_hrtime tt = e->timeout_at - now;
        for (j = i; j > 0 && tt <= PQ_BUCKET_TIME(j - 1);) {
          j--;
        }
        e->in_heap = j;
        after[j].enqueue(e);
      }
    }
  }
}
