/** @file

    Unit tests for BRAVO

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

#include "catch.hpp"
#include "tsutil/Bravo.h"

#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <thread>

using namespace std::chrono_literals;

TEST_CASE("BRAVO - simple check", "[libts][BRAVO]")
{
  SECTION("reader-reader")
  {
    ts::bravo::shared_mutex mutex;
    ts::bravo::shared_lock<ts::bravo::shared_mutex> lock(mutex);
    CHECK(lock.owns_lock() == true);

    std::thread t{[](ts::bravo::shared_mutex &mutex) {
                    ts::bravo::Token token{0};
                    CHECK(mutex.try_lock_shared(token) == true);
                    mutex.unlock_shared(token);
                  },
                  std::ref(mutex)};

    t.join();
  }

  SECTION("reader-writer")
  {
    ts::bravo::shared_mutex mutex;
    ts::bravo::shared_lock<ts::bravo::shared_mutex> lock(mutex);
    CHECK(lock.owns_lock() == true);

    std::thread t{[](ts::bravo::shared_mutex &mutex) { CHECK(mutex.try_lock() == false); }, std::ref(mutex)};

    t.join();
  }

  SECTION("writer-reader")
  {
    ts::bravo::shared_mutex mutex;
    std::lock_guard<ts::bravo::shared_mutex> lock(mutex);

    std::thread t{[](ts::bravo::shared_mutex &mutex) {
                    ts::bravo::Token token{0};
                    CHECK(mutex.try_lock_shared(token) == false);
                    CHECK(token == 0);
                  },
                  std::ref(mutex)};

    t.join();
  }

  SECTION("writer-writer")
  {
    ts::bravo::shared_mutex mutex;
    std::lock_guard<ts::bravo::shared_mutex> lock(mutex);

    std::thread t{[](ts::bravo::shared_mutex &mutex) { CHECK(mutex.try_lock() == false); }, std::ref(mutex)};

    t.join();
  }
}

TEST_CASE("BRAVO - multiple try-lock", "[libts][BRAVO]")
{
  SECTION("rwrw")
  {
    ts::bravo::shared_mutex mutex;
    int i = 0;

    {
      ts::bravo::Token token{0};
      CHECK(mutex.try_lock_shared(token));
      CHECK(i == 0);
      mutex.unlock_shared(token);
    }

    {
      CHECK(mutex.try_lock());
      CHECK(++i == 1);
      mutex.unlock();
    }

    {
      ts::bravo::Token token{0};
      CHECK(mutex.try_lock_shared(token));
      CHECK(i == 1);
      mutex.unlock_shared(token);
    }

    {
      CHECK(mutex.try_lock());
      CHECK(++i == 2);
      mutex.unlock();
    }

    CHECK(i == 2);
  }
}

TEST_CASE("BRAVO - check with race", "[libts][BRAVO]")
{
  SECTION("reader-reader")
  {
    ts::bravo::shared_mutex mutex;
    int i = 0;

    std::thread t1{[&](ts::bravo::shared_mutex &mutex) {
                     ts::bravo::shared_lock<ts::bravo::shared_mutex> lock(mutex);
                     CHECK(lock.owns_lock() == true);
                     CHECK(i == 0);
                   },
                   std::ref(mutex)};

    std::thread t2{[&](ts::bravo::shared_mutex &mutex) {
                     ts::bravo::shared_lock<ts::bravo::shared_mutex> lock(mutex);
                     CHECK(lock.owns_lock() == true);
                     CHECK(i == 0);
                   },
                   std::ref(mutex)};

    t1.join();
    t2.join();

    CHECK(i == 0);
  }

  SECTION("reader-writer")
  {
    ts::bravo::shared_mutex mutex;
    int i = 0;

    std::thread t1{[&](ts::bravo::shared_mutex &mutex) {
                     ts::bravo::shared_lock<ts::bravo::shared_mutex> lock(mutex);
                     CHECK(lock.owns_lock() == true);
                     CHECK(i == 0);
                     std::this_thread::sleep_for(100ms);
                   },
                   std::ref(mutex)};

    std::thread t2{[&](ts::bravo::shared_mutex &mutex) {
                     std::this_thread::sleep_for(50ms);
                     std::lock_guard<ts::bravo::shared_mutex> lock(mutex);
                     CHECK(++i == 1);
                   },
                   std::ref(mutex)};

    t1.join();
    t2.join();

    CHECK(i == 1);
  }

  SECTION("writer-reader")
  {
    ts::bravo::shared_mutex mutex;
    int i = 0;

    std::thread t1{[&](ts::bravo::shared_mutex &mutex) {
                     std::lock_guard<ts::bravo::shared_mutex> lock(mutex);
                     std::this_thread::sleep_for(100ms);
                     CHECK(++i == 1);
                   },
                   std::ref(mutex)};

    std::thread t2{[&](ts::bravo::shared_mutex &mutex) {
                     std::this_thread::sleep_for(50ms);
                     ts::bravo::shared_lock<ts::bravo::shared_mutex> lock(mutex);
                     CHECK(lock.owns_lock() == true);
                     CHECK(i == 1);
                   },
                   std::ref(mutex)};

    t1.join();
    t2.join();

    CHECK(i == 1);
  }

  SECTION("writer-writer")
  {
    ts::bravo::shared_mutex mutex;
    int i = 0;

    std::thread t1{[&](ts::bravo::shared_mutex &mutex) {
                     std::lock_guard<ts::bravo::shared_mutex> lock(mutex);
                     std::this_thread::sleep_for(100ms);
                     CHECK(++i == 1);
                   },
                   std::ref(mutex)};

    std::thread t2{[&](ts::bravo::shared_mutex &mutex) {
                     std::this_thread::sleep_for(50ms);
                     std::lock_guard<ts::bravo::shared_mutex> lock(mutex);
                     CHECK(++i == 2);
                   },
                   std::ref(mutex)};

    t1.join();
    t2.join();

    CHECK(i == 2);
  }
}
