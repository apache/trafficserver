/**
  @file SharedExtendible.cc

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

#include "SharedAccess.h"

WriterMutex_t &
SharedAccessMutex(void const *ptr)
{
  static LockPool<WriterMutex_t> read_locks(64);
  static_assert(sizeof(void *) == sizeof(size_t));
  return read_locks.getMutex(reinterpret_cast<size_t>(ptr));
}

WriterMutex_t &
SharedWriterMutex(void const *ptr)
{
  static LockPool<WriterMutex_t> write_locks(64);
  static_assert(sizeof(void *) == sizeof(size_t));
  return write_locks.getMutex(reinterpret_cast<size_t>(ptr));
}