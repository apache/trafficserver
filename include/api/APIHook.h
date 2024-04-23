/** @file

  Internal SDK stuff

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

#include "ts/InkAPIPrivateIOCore.h"

#include "tscore/List.h"

/// A single API hook that can be invoked.
class APIHook
{
public:
  INKContInternal *m_cont;
  int              invoke(int event, void *edata) const;
  APIHook         *next() const;
  APIHook         *prev() const;
  LINK(APIHook, m_link);

  // This is like invoke(), but allows for blocking on continuation mutexes.  It is a hack, calling it can block
  // the calling thread.  Hooks that require this should be reimplemented, modeled on the hook handling in HttpSM.cc .
  // That is, try to lock the mutex, and reschedule the continuation if the mutex cannot be locked.
  //
  int blocking_invoke(int event, void *edata) const;
};
