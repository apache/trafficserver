/** @file

    Unit tests for AltMutex.h.

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

#include <catch.hpp>
#include <tscpp/util/AltMutex.h>

namespace
{
int i{0};

ts::AltMutex mtx;

void
incr_i(int wait_value)
{
  bool done = false;

  do {
    mtx.lock();

    if (i == wait_value) {
      ++i;
      done = true;
    }

    mtx.unlock();

  } while (!done);
}

void
thread2()
{
  incr_i(1);
}

void
thread3()
{
  incr_i(2);
}

} // end anonymous namespace

void
_ink_assert(char const *, char const *, int)
{
  std::exit(1);
}

TEST_CASE("AltMutex", "[AM]")
{
  std::thread t2(thread2);
  std::thread t3(thread3);

  incr_i(0);

  t2.join();
  t3.join();

  REQUIRE(i == 3);
}
