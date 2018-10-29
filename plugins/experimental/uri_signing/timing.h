/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <stdint.h>
#include <time.h>

struct timer {
  int started;
  struct timespec start;
};

inline void
start_timer(struct timer *t)
{
  t->started = !clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t->start);
}

inline int64_t
mark_timer(struct timer *t)
{
  struct timespec now;
  if (!t->started) {
    return 0;
  }
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now)) {
    return 0;
  }
  return (now.tv_sec - t->start.tv_sec) * (int64_t)1000000000 - (int64_t)t->start.tv_nsec + (int64_t)now.tv_nsec;
}
