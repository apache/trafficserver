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

  Basic Locks for Threads



**************************************************************************/
#include "P_EventSystem.h"
#include "tscore/Diags.h"

ClassAllocator<ProxyMutex> mutexAllocator("mutexAllocator");

void
lock_waiting(const SourceLocation &srcloc, const char *handler)
{
  if (is_diags_on("locks")) {
    char buf[128];
    fprintf(stderr, "WARNING: waiting on lock %s for %s\n", srcloc.str(buf, sizeof(buf)), handler ? handler : "UNKNOWN");
  }
}

void
lock_holding(const SourceLocation &srcloc, const char *handler)
{
  if (is_diags_on("locks")) {
    char buf[128];
    fprintf(stderr, "WARNING: holding lock %s too long for %s\n", srcloc.str(buf, sizeof(buf)), handler ? handler : "UNKNOWN");
  }
}

void
lock_taken(const SourceLocation &srcloc, const char *handler)
{
  if (is_diags_on("locks")) {
    char buf[128];
    fprintf(stderr, "WARNING: lock %s taken too many times for %s\n", srcloc.str(buf, sizeof(buf)), handler ? handler : "UNKNOWN");
  }
}

#ifdef LOCK_CONTENTION_PROFILING
void
ProxyMutex::print_lock_stats(int flag)
{
  if (flag) {
    if (total_acquires < 10)
      return;
    printf("Lock Stats (Dying):successful %d (%.2f%%), unsuccessful %d (%.2f%%) blocking %d \n", successful_nonblocking_acquires,
           (nonblocking_acquires > 0 ? successful_nonblocking_acquires * 100.0 / nonblocking_acquires : 0.0),
           unsuccessful_nonblocking_acquires,
           (nonblocking_acquires > 0 ? unsuccessful_nonblocking_acquires * 100.0 / nonblocking_acquires : 0.0), blocking_acquires);
    fflush(stdout);
  } else {
    if (!(total_acquires % 100)) {
      printf("Lock Stats (Alive):successful %d (%.2f%%), unsuccessful %d (%.2f%%) blocking %d \n", successful_nonblocking_acquires,
             (nonblocking_acquires > 0 ? successful_nonblocking_acquires * 100.0 / nonblocking_acquires : 0.0),
             unsuccessful_nonblocking_acquires,
             (nonblocking_acquires > 0 ? unsuccessful_nonblocking_acquires * 100.0 / nonblocking_acquires : 0.0),
             blocking_acquires);
      fflush(stdout);
    }
  }
}
#endif // LOCK_CONTENTION_PROFILING
