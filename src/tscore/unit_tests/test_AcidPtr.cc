/** @file
  Test file for AcidPtr
  @section license License
  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at
      http://www.apache.org/licenses/LICENSE-2.0
  Unless REQUIRE by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */

#include "catch.hpp"

#include "tscore/AcidPtr.h"
#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <ctime>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <chrono>

using namespace std;

TEST_CASE("AcidPtr Atomicity")
{
  // fail if skew is detected.
  constexpr int N = 1000; // Number of threads
  constexpr int K = 50;   // Size of data sample.
  AcidPtr<vector<int>> ptr(new vector<int>(K));
  atomic<int> errors{0};
  atomic<unsigned> count{0};
  condition_variable gate;
  mutex gate_mutex;

  auto job_read_write = [&]() {
    {
      unique_lock<mutex> gate_lock(gate_mutex);
      gate.wait(gate_lock);
    }
    int r = rand();
    AcidCommitPtr<vector<int>> cptr(ptr);
    int old = (*cptr)[0];
    for (int &i : *cptr) {
      if (i != old) {
        errors++;
      }
      i = r;
    }
    ++count;
  };

  auto job_read = [&]() {
    {
      unique_lock<mutex> gate_lock(gate_mutex);
      gate.wait(gate_lock);
    }
    auto sptr = ptr.getPtr();
    int old   = (*sptr)[0];
    for (int const &i : *sptr) {
      if (i != old) {
        errors++;
      }
    }
    ++count;
  };

  array<thread, N> writers;
  array<thread, N> readers;

  // Use a for loop so the threads start in pairs.
  for (int i = 0; i < N; i++) {
    writers[i] = thread(job_read_write);
    readers[i] = thread(job_read);
    gate.notify_all();
  }

  while (count < 2 * N) {
    gate.notify_all();
    this_thread::sleep_for(chrono::milliseconds{10});
  }

  for (auto &t : readers) {
    t.join();
  }

  for (auto &t : writers) {
    t.join();
  }

  REQUIRE(errors == 0); // skew detected
}

TEST_CASE("AcidPtr Isolation")
{
  AcidPtr<int> p;
  REQUIRE(p.getPtr() != nullptr);
  REQUIRE(p.getPtr().get() != nullptr);
  {
    AcidCommitPtr<int> w(p);
    *w = 40;
  }
  CHECK(*p.getPtr() == 40);
  {
    AcidCommitPtr<int> w = p;
    *w += 1;
    CHECK(*p.getPtr() == 40); // new value not committed until end of scope
  }
  CHECK(*p.getPtr() == 41);
  {
    *AcidCommitPtr<int>(p) += 1; // leaves scope immediately if not named.
    CHECK(*p.getPtr() == 42);
  }
  CHECK(*p.getPtr() == 42);
}

TEST_CASE("AcidPtr persistence")
{
  AcidPtr<int> p(new int(40));
  std::shared_ptr<const int> r1, r2, r3, r4;
  REQUIRE(p.getPtr() != nullptr);
  r1 = p.getPtr();
  {
    AcidCommitPtr<int> w = p;
    r2                   = p.getPtr();
    *w += 1; // update p at end of scope
  }
  r3 = p.getPtr();
  {
    *AcidCommitPtr<int>(p) += 1; // leaves scope immediately if not named.
    r4 = p.getPtr();
  }
  CHECK(*r1 == 40); // references to data are still valid, but inconsistent. (todo: rename AcidPtr to AiPtr?)
  CHECK(*r2 == 40);
  CHECK(*r3 == 41);
  CHECK(*r4 == 42);
}

TEST_CASE("AcidPtr Abort")
{
  AcidPtr<int> p;
  {
    AcidCommitPtr<int> w(p);
    *w = 40;
  }
  CHECK(*p.getPtr() == 40);
  {
    AcidCommitPtr<int> w = p;
    *w += 1;
    w.abort();
    CHECK(w == nullptr);
  }
  CHECK(*p.getPtr() == 40);
}
