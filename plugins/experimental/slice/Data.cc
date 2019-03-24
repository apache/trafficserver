/** @file
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

#include "Data.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

namespace
{
std::mutex mutex;
int64_t inplay = 0;
std::unique_ptr<std::thread> thread;
} // namespace

void
monitor()
{
  std::lock_guard<std::mutex> guard(mutex);
  //	while (0 < inplay)
  while (true) {
    mutex.unlock();
    std::this_thread::sleep_for(std::chrono::seconds(10));
    std::cerr << "Inplay: " << inplay << std::endl;
    mutex.lock();
  }
  //	thread.release();
}

void
incrData()
{
  std::lock_guard<std::mutex> const guard(mutex);
  if (!thread) {
    thread.reset(new std::thread(monitor));
  }

  ++inplay;
}

void
decrData()
{
  std::lock_guard<std::mutex> const guard(mutex);
  --inplay;
  assert(0 <= inplay);
}
