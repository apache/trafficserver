/** @file
  Test file for Extendible
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

#include "ts/AcidPtr.h"
#include <iostream>
#include <string>
#include <vector>
#include <ctime>
#include <thread>
#include <atomic>

using namespace std;

TEST_CASE("AcidPtr Atomicity")
{
  // fail if skew is detected.
  const int N = 1000;
  AcidPtr<vector<int>> ptr(new vector<int>(50));
  std::thread workers[N];
  atomic<int> errors = {0};

  auto job_read_write = [&ptr, &errors]() {
    int r = rand();
    AcidCommitPtr<vector<int>> cptr(ptr);
    int old = (*cptr)[0];
    for (int &i : *cptr) {
      if (i != old) {
        errors++;
      }
      i = r;
    }
  };
  auto job_read = [&ptr, &errors]() {
    auto sptr = ptr.getPtr();
    int old   = (*sptr)[0];
    for (int const &i : *sptr) {
      if (i != old) {
        errors++;
      }
    }
  };

  std::thread writers[N];
  std::thread readers[N];

  for (int i = 0; i < N; i++) {
    writers[i] = std::thread(job_read_write);
    readers[i] = std::thread(job_read);
  }

  for (int i = 0; i < N; i++) {
    writers[i].join();
    readers[i].join();
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
    CHECK(*p.getPtr() == 40); // new value not commited until end of scope
  }
  CHECK(*p.getPtr() == 41);
  {
    *AcidCommitPtr<int>(p) += 1; // leaves scope immediately if not named.
    CHECK(*p.getPtr() == 42);
  }
  CHECK(*p.getPtr() == 42);
}
