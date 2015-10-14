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

#include "ts/ink_error.h"
#include "ts/ink_defs.h"
#include <assert.h>
#include "stdio.h"
#include "ts/ink_mutex.h"

// Define the _g_mattr first to avoid static initialization order fiasco.
x_pthread_mutexattr_t _g_mattr;

ink_mutex __global_death = PTHREAD_MUTEX_INITIALIZER;
