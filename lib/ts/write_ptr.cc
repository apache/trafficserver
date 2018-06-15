/**
  @file Extendible.cc

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

#include "write_ptr.h"

const int READLOCKCOUNT = 61; // a prime larger than number of readers

ReadMutex_t &
ReadMemMutex(void const *ptr)
{
  static LockPool<ReadMutex_t> read_locks(READLOCKCOUNT);
  static_assert(sizeof(void *) == sizeof(size_t));
  return read_locks.getMutex(reinterpret_cast<size_t>(ptr));
}

const int WRITELOCKCOUNT = 31; // a prime larger than number of writers

WriteMutex_t &
WriteMemMutex(void const *ptr)
{
  static LockPool<WriteMutex_t> write_locks(WRITELOCKCOUNT);
  static_assert(sizeof(void *) == sizeof(size_t));
  return write_locks.getMutex(reinterpret_cast<size_t>(ptr));
}