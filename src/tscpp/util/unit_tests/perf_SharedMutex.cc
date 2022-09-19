/** @file

    Performance testing for ts::shared_mutex and ts::scalable_shared_mutex,
    with std::shared_mutex as a benchmark.

    To build with gcc or clang:

    CC -Wall -Wextra -pedantic -Wno-format -O3 -std=c++17 -I../../../../include -Dlinux perf_SharedMutex.cc -lstdc++ \
      -lpthread

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

#include <shared_mutex>
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>

#include <tscpp/util/TsSharedMutex.h>
#include <tscpp/util/TsScalableSharedMutex.h>

using namespace std::chrono_literals;

namespace
{
auto const Wait_period{5s};
unsigned const Num_threads{256};

template <class SharedMtx> class Test
{
public:
  static void
  x()
  {
    std::thread thread[Num_threads];

    _ready_thread_count = 0;
    _start              = false;
    _stop               = false;

    for (unsigned i{0}; i < Num_threads; ++i) {
      thread[i] = std::thread{_thread_func, i};
    }
    while (_ready_thread_count < Num_threads) {
      std::this_thread::yield();
    }

    _start = true;
    std::this_thread::sleep_for(Wait_period);
    _stop = true;

    for (unsigned i{0}; i < Num_threads; ++i) {
      thread[i].join();
    }
    unsigned long long max{0}, min{static_cast<unsigned long long>(0) - 1}, total{0};
    for (unsigned i{0}; i < Num_threads; ++i) {
      total += _lock_count[i].value;
      if (_lock_count[i].value < min) {
        min = _lock_count[i].value;
      }
      if (_lock_count[i].value > max) {
        max = _lock_count[i].value;
      }
    }
    std::cout << "num_threads=" << Num_threads << " max_locks=" << max << " min_locks=" << min
              << " average=" << ((total + (Num_threads / 2)) / Num_threads) << '\n';
  }

private:
  inline static SharedMtx _mtx;

  // Put each count in it's own cache line.
  //
  union _LC {
    unsigned long long value;
    char spacer[ts::CACHE_LINE_SIZE_LCM];
  };

  inline static std::atomic<bool> _start, _stop;
  inline static std::atomic<unsigned> _ready_thread_count;
  alignas(_LC) inline static _LC _lock_count[Num_threads];

  static void
  _thread_func(unsigned thread_idx)
  {
    _lock_count[thread_idx].value = 0;

    ++_ready_thread_count;

    // Don't put the overhead of the first call to this in the timing loop.
    //
    ts::DenseThreadId::self();

    while (!_start) {
      std::this_thread::yield();
    }
    while (!_stop) {
      _mtx.lock_shared();
      _mtx.unlock_shared();
      ++_lock_count[thread_idx].value;
    }
  }
};

} // end anonymous namespace

int
main()
{
  ts::DenseThreadId::set_num_possible_values(Num_threads + 42);

  std::cout << "std::shared_mutex\n";
  Test<std::shared_mutex>::x();

  std::cout << "\nts::shared_mutex\n";
  Test<ts::shared_mutex>::x();

  std::cout << "\nts::scalable_shared_mutex\n";
  Test<ts::scalable_shared_mutex>::x();

  return 0;
}

#include <cstdlib>

// Stubs.
//
LogMessage::LogMessage(bool) : Throttler{666ms}, _throttling_value_is_explicitly_set{false}, _is_throttled{false} {}
void
LogMessage::message(DiagsLevel, SourceLocation const &, char const *, ...)
{
}
Throttler::Throttler(std::chrono::duration<long, std::ratio<1l, 1000000l>>) {}
bool
Throttler::is_throttled(unsigned long &)
{
  return false;
}
void Throttler::set_throttling_interval(std::chrono::duration<long, std::ratio<1l, 1000000l>>) {}
uint64_t
Throttler::reset_counter()
{
  return 0;
}
extern "C" tsapi void
TSFatal(const char *, ...)
{
  std::abort();
}
extern "C" tsapi int
_TSAssert(const char *, const char *, int)
{
  std::abort();
  return 0;
}
