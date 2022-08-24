/** @file

  Public RecMutex declarations

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

#pragma once

#include "tscore/ink_mutex.h"
#include "tscore/ink_thread.h"

/**
  A wrapper to ink_mutex class. It allows multiple acquire of mutex lock
  by the SAME thread. This is a trimmed down version of ProxyMutex.

*/
struct RecMutex {
  size_t nthread_holding;
  ink_thread thread_holding;
  ink_mutex the_mutex;
};

void rec_mutex_init(RecMutex *m, const char *name = nullptr);
void rec_mutex_destroy(RecMutex *m);
void rec_mutex_acquire(RecMutex *m);
void rec_mutex_release(RecMutex *m);
