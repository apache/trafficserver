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

#include <catch2/catch_test_macros.hpp>
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
    ts::bravo::shared_mutex                         mutex;
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
    ts::bravo::shared_mutex                         mutex;
    ts::bravo::shared_lock<ts::bravo::shared_mutex> lock(mutex);
    CHECK(lock.owns_lock() == true);

    std::thread t{[](ts::bravo::shared_mutex &mutex) { CHECK(mutex.try_lock() == false); }, std::ref(mutex)};

    t.join();
  }

  SECTION("writer-reader")
  {
    ts::bravo::shared_mutex                  mutex;
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
    ts::bravo::shared_mutex                  mutex;
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
    int                     i = 0;

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
    int                     i = 0;

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
    int                     i = 0;

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
    int                     i = 0;

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
    int                     i = 0;

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

TEST_CASE("Recursive BRAVO - exclusive lock", "[libts][BRAVO]")
{
  SECTION("single lock/unlock")
  {
    ts::bravo::recursive_shared_mutex mutex;
    mutex.lock();
    mutex.unlock();
  }

  SECTION("recursive lock/unlock")
  {
    ts::bravo::recursive_shared_mutex mutex;
    mutex.lock();
    mutex.lock();
    mutex.lock();
    mutex.unlock();
    mutex.unlock();
    mutex.unlock();
  }

  SECTION("try_lock by owner succeeds")
  {
    ts::bravo::recursive_shared_mutex mutex;
    mutex.lock();
    CHECK(mutex.try_lock() == true);
    mutex.unlock();
    mutex.unlock();
  }

  SECTION("try_lock by non-owner fails")
  {
    ts::bravo::recursive_shared_mutex mutex;
    mutex.lock();

    std::thread t{[&mutex]() { CHECK(mutex.try_lock() == false); }};
    t.join();

    mutex.unlock();
  }

  SECTION("recursive try_lock")
  {
    ts::bravo::recursive_shared_mutex mutex;
    CHECK(mutex.try_lock() == true);
    CHECK(mutex.try_lock() == true);
    CHECK(mutex.try_lock() == true);
    mutex.unlock();
    mutex.unlock();
    mutex.unlock();
  }

  SECTION("writer-writer blocking")
  {
    ts::bravo::recursive_shared_mutex mutex;
    int                               i = 0;

    std::thread t1{[&]() {
      std::lock_guard<ts::bravo::recursive_shared_mutex> lock(mutex);
      std::this_thread::sleep_for(100ms);
      CHECK(++i == 1);
    }};

    std::thread t2{[&]() {
      std::this_thread::sleep_for(50ms);
      std::lock_guard<ts::bravo::recursive_shared_mutex> lock(mutex);
      CHECK(++i == 2);
    }};

    t1.join();
    t2.join();

    CHECK(i == 2);
  }
}

TEST_CASE("Recursive BRAVO - shared lock", "[libts][BRAVO]")
{
  SECTION("single shared lock/unlock")
  {
    ts::bravo::recursive_shared_mutex mutex;
    ts::bravo::Token                  token{0};
    mutex.lock_shared(token);
    mutex.unlock_shared(token);
  }

  SECTION("recursive shared lock/unlock")
  {
    ts::bravo::recursive_shared_mutex mutex;
    ts::bravo::Token                  token1{0};
    ts::bravo::Token                  token2{0};
    ts::bravo::Token                  token3{0};
    mutex.lock_shared(token1);
    mutex.lock_shared(token2);
    mutex.lock_shared(token3);
    // All tokens should be the same (cached)
    CHECK(token1 == token2);
    CHECK(token2 == token3);
    mutex.unlock_shared(token3);
    mutex.unlock_shared(token2);
    mutex.unlock_shared(token1);
  }

  SECTION("try_lock_shared recursive")
  {
    ts::bravo::recursive_shared_mutex mutex;
    ts::bravo::Token                  token1{0};
    ts::bravo::Token                  token2{0};
    CHECK(mutex.try_lock_shared(token1) == true);
    CHECK(mutex.try_lock_shared(token2) == true);
    CHECK(token1 == token2);
    mutex.unlock_shared(token2);
    mutex.unlock_shared(token1);
  }

  SECTION("multiple readers concurrent")
  {
    ts::bravo::recursive_shared_mutex mutex;
    int                               i = 0;

    std::thread t1{[&]() {
      ts::bravo::Token token{0};
      mutex.lock_shared(token);
      CHECK(i == 0);
      std::this_thread::sleep_for(50ms);
      mutex.unlock_shared(token);
    }};

    std::thread t2{[&]() {
      ts::bravo::Token token{0};
      mutex.lock_shared(token);
      CHECK(i == 0);
      std::this_thread::sleep_for(50ms);
      mutex.unlock_shared(token);
    }};

    t1.join();
    t2.join();

    CHECK(i == 0);
  }

  SECTION("shared blocks exclusive")
  {
    ts::bravo::recursive_shared_mutex mutex;
    ts::bravo::Token                  token{0};
    mutex.lock_shared(token);

    std::thread t{[&mutex]() { CHECK(mutex.try_lock() == false); }};
    t.join();

    mutex.unlock_shared(token);
  }

  SECTION("exclusive blocks shared")
  {
    ts::bravo::recursive_shared_mutex mutex;
    mutex.lock();

    std::thread t{[&mutex]() {
      ts::bravo::Token token{0};
      CHECK(mutex.try_lock_shared(token) == false);
    }};
    t.join();

    mutex.unlock();
  }
}

TEST_CASE("Recursive BRAVO - mixed lock scenarios", "[libts][BRAVO]")
{
  SECTION("downgrade: exclusive owner can acquire shared lock")
  {
    ts::bravo::recursive_shared_mutex mutex;
    mutex.lock();

    // While holding exclusive lock, we can acquire shared lock
    ts::bravo::Token token{0};
    mutex.lock_shared(token);
    CHECK(token == 0); // Special token for downgrade

    mutex.unlock_shared(token);
    mutex.unlock();
  }

  SECTION("downgrade: try_lock_shared succeeds for exclusive owner")
  {
    ts::bravo::recursive_shared_mutex mutex;
    mutex.lock();

    ts::bravo::Token token{0};
    CHECK(mutex.try_lock_shared(token) == true);
    CHECK(token == 0); // Special token for downgrade

    mutex.unlock_shared(token);
    mutex.unlock();
  }

  SECTION("upgrade prevention: try_lock fails when holding shared lock")
  {
    ts::bravo::recursive_shared_mutex mutex;
    ts::bravo::Token                  token{0};
    mutex.lock_shared(token);

    // Cannot upgrade: try_lock should fail
    CHECK(mutex.try_lock() == false);

    mutex.unlock_shared(token);
  }

  SECTION("downgrade with multiple shared locks")
  {
    ts::bravo::recursive_shared_mutex mutex;
    mutex.lock();

    ts::bravo::Token token1{0};
    ts::bravo::Token token2{0};
    mutex.lock_shared(token1);
    mutex.lock_shared(token2);

    mutex.unlock_shared(token2);
    mutex.unlock_shared(token1);
    mutex.unlock();
  }

  SECTION("proper unlock ordering: shared then exclusive")
  {
    ts::bravo::recursive_shared_mutex mutex;
    mutex.lock();

    ts::bravo::Token token{0};
    mutex.lock_shared(token);

    // Unlock shared first, then exclusive
    mutex.unlock_shared(token);
    mutex.unlock();

    // Mutex should be fully unlocked now
    CHECK(mutex.try_lock() == true);
    mutex.unlock();
  }

  SECTION("nested exclusive locks with shared in between")
  {
    ts::bravo::recursive_shared_mutex mutex;
    mutex.lock();
    mutex.lock(); // Recursive exclusive

    ts::bravo::Token token{0};
    mutex.lock_shared(token);

    mutex.unlock_shared(token);
    mutex.unlock(); // Second exclusive
    mutex.unlock(); // First exclusive

    // Mutex should be fully unlocked now
    CHECK(mutex.try_lock() == true);
    mutex.unlock();
  }
}

TEST_CASE("Recursive BRAVO - BRAVO optimizations", "[libts][BRAVO]")
{
  SECTION("first shared lock gets token from underlying BRAVO mutex")
  {
    ts::bravo::recursive_shared_mutex mutex;
    ts::bravo::Token                  token{0};
    mutex.lock_shared(token);
    // Token should be set by underlying BRAVO mutex (0 = slow path, >0 = fast path)
    // We can't guarantee which path is taken, but the lock should succeed
    mutex.unlock_shared(token);
  }

  SECTION("recursive shared locks reuse cached token")
  {
    ts::bravo::recursive_shared_mutex mutex;
    ts::bravo::Token                  token1{0};
    ts::bravo::Token                  token2{0};
    ts::bravo::Token                  token3{0};

    mutex.lock_shared(token1);
    mutex.lock_shared(token2);
    mutex.lock_shared(token3);

    // All tokens should be identical (cached from first lock)
    CHECK(token1 == token2);
    CHECK(token2 == token3);

    mutex.unlock_shared(token3);
    mutex.unlock_shared(token2);
    mutex.unlock_shared(token1);
  }

  SECTION("writer revocation then reader works")
  {
    ts::bravo::recursive_shared_mutex mutex;

    // First, acquire and release a shared lock to enable read_bias
    {
      ts::bravo::Token token{0};
      mutex.lock_shared(token);
      mutex.unlock_shared(token);
    }

    // Writer acquires lock (triggers revocation)
    mutex.lock();
    mutex.unlock();

    // Reader should still work after writer releases
    {
      ts::bravo::Token token{0};
      mutex.lock_shared(token);
      mutex.unlock_shared(token);
    }
  }

  SECTION("multiple readers then writer then readers")
  {
    ts::bravo::recursive_shared_mutex mutex;
    std::atomic<int>                  readers_done{0};

    // Start multiple readers
    std::thread t1{[&]() {
      ts::bravo::Token token{0};
      mutex.lock_shared(token);
      std::this_thread::sleep_for(50ms);
      mutex.unlock_shared(token);
      ++readers_done;
    }};

    std::thread t2{[&]() {
      ts::bravo::Token token{0};
      mutex.lock_shared(token);
      std::this_thread::sleep_for(50ms);
      mutex.unlock_shared(token);
      ++readers_done;
    }};

    // Wait for readers to finish
    t1.join();
    t2.join();
    CHECK(readers_done == 2);

    // Writer acquires lock
    mutex.lock();
    mutex.unlock();

    // More readers after writer
    std::thread t3{[&]() {
      ts::bravo::Token token{0};
      mutex.lock_shared(token);
      mutex.unlock_shared(token);
      ++readers_done;
    }};

    std::thread t4{[&]() {
      ts::bravo::Token token{0};
      mutex.lock_shared(token);
      mutex.unlock_shared(token);
      ++readers_done;
    }};

    t3.join();
    t4.join();
    CHECK(readers_done == 4);
  }

  SECTION("recursive shared lock with concurrent writer")
  {
    ts::bravo::recursive_shared_mutex mutex;
    std::atomic<bool>                 writer_done{false};

    // Reader thread with recursive locks
    std::thread reader{[&]() {
      ts::bravo::Token token1{0};
      ts::bravo::Token token2{0};
      mutex.lock_shared(token1);
      mutex.lock_shared(token2); // Recursive
      CHECK(token1 == token2);   // Should be same cached token
      std::this_thread::sleep_for(100ms);
      mutex.unlock_shared(token2);
      mutex.unlock_shared(token1);
    }};

    // Writer thread tries to acquire after reader starts
    std::thread writer{[&]() {
      std::this_thread::sleep_for(50ms);
      mutex.lock();
      writer_done = true;
      mutex.unlock();
    }};

    reader.join();
    writer.join();
    CHECK(writer_done == true);
  }
}

TEST_CASE("Recursive BRAVO - stress test", "[libts][BRAVO]")
{
  SECTION("concurrent readers with recursive locks")
  {
    ts::bravo::recursive_shared_mutex mutex;
    std::atomic<int>                  counter{0};
    constexpr int                     NUM_THREADS    = 8;
    constexpr int                     NUM_ITERATIONS = 1000;

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
      threads.emplace_back([&]() {
        for (int j = 0; j < NUM_ITERATIONS; ++j) {
          ts::bravo::Token token1{0};
          ts::bravo::Token token2{0};
          mutex.lock_shared(token1);
          mutex.lock_shared(token2); // Recursive
          ++counter;
          mutex.unlock_shared(token2);
          mutex.unlock_shared(token1);
        }
      });
    }

    for (auto &t : threads) {
      t.join();
    }

    CHECK(counter == NUM_THREADS * NUM_ITERATIONS);
  }

  SECTION("concurrent writers with recursive locks")
  {
    ts::bravo::recursive_shared_mutex mutex;
    int                               counter        = 0;
    constexpr int                     NUM_THREADS    = 4;
    constexpr int                     NUM_ITERATIONS = 500;

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
      threads.emplace_back([&]() {
        for (int j = 0; j < NUM_ITERATIONS; ++j) {
          mutex.lock();
          mutex.lock(); // Recursive
          ++counter;
          mutex.unlock();
          mutex.unlock();
        }
      });
    }

    for (auto &t : threads) {
      t.join();
    }

    CHECK(counter == NUM_THREADS * NUM_ITERATIONS);
  }

  SECTION("mixed readers and writers")
  {
    ts::bravo::recursive_shared_mutex mutex;
    std::atomic<int>                  read_counter{0};
    int                               write_counter  = 0;
    constexpr int                     NUM_READERS    = 6;
    constexpr int                     NUM_WRITERS    = 2;
    constexpr int                     NUM_ITERATIONS = 500;

    std::vector<std::thread> threads;

    // Reader threads
    for (int i = 0; i < NUM_READERS; ++i) {
      threads.emplace_back([&]() {
        for (int j = 0; j < NUM_ITERATIONS; ++j) {
          ts::bravo::Token token{0};
          mutex.lock_shared(token);
          ++read_counter;
          mutex.unlock_shared(token);
        }
      });
    }

    // Writer threads
    for (int i = 0; i < NUM_WRITERS; ++i) {
      threads.emplace_back([&]() {
        for (int j = 0; j < NUM_ITERATIONS; ++j) {
          mutex.lock();
          ++write_counter;
          mutex.unlock();
        }
      });
    }

    for (auto &t : threads) {
      t.join();
    }

    CHECK(read_counter == NUM_READERS * NUM_ITERATIONS);
    CHECK(write_counter == NUM_WRITERS * NUM_ITERATIONS);
  }

  SECTION("recursive mixed locks under contention")
  {
    ts::bravo::recursive_shared_mutex mutex;
    std::atomic<int>                  counter{0};
    constexpr int                     NUM_THREADS    = 4;
    constexpr int                     NUM_ITERATIONS = 200;

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
      threads.emplace_back([&, i]() {
        for (int j = 0; j < NUM_ITERATIONS; ++j) {
          if (i % 2 == 0) {
            // Even threads: exclusive with downgrade
            mutex.lock();
            mutex.lock(); // Recursive exclusive
            ts::bravo::Token token{0};
            mutex.lock_shared(token); // Downgrade
            ++counter;
            mutex.unlock_shared(token);
            mutex.unlock();
            mutex.unlock();
          } else {
            // Odd threads: shared recursive
            ts::bravo::Token token1{0};
            ts::bravo::Token token2{0};
            mutex.lock_shared(token1);
            mutex.lock_shared(token2);
            ++counter;
            mutex.unlock_shared(token2);
            mutex.unlock_shared(token1);
          }
        }
      });
    }

    for (auto &t : threads) {
      t.join();
    }

    CHECK(counter == NUM_THREADS * NUM_ITERATIONS);
  }
}
