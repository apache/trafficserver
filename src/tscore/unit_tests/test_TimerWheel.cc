/** @file

  A brief file description

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

#include <vector>

#include "tscore/TimerWheel.h"
#include "tscore/ink_hrtime.h"

namespace
{

// Minimal element satisfying the TimerWheel contract.
struct Conn {
  int            id       = 0;
  ink_hrtime     deadline = 0; // the "true" deadline the wheel re-reads
  TimerWheelHook timer_hook;

  LINK(Conn, timer_link);

  Conn(int i, ink_hrtime d) : id(i), deadline(d) {}
};

using Wheel = TimerWheel<Conn>;

// Fire callback that records ids, and reports each element's true deadline.
struct Recorder {
  std::vector<int> fired;

  ink_hrtime
  deadline_of(Conn *c) const
  {
    return c->deadline;
  }

  void
  operator()(Conn *c)
  {
    fired.push_back(c->id);
  }
};

} // namespace

TEST_CASE("TimerWheel fires an element once its deadline passes", "[libts][TimerWheel]")
{
  ink_hrtime const t0 = HRTIME_SECONDS(1000);
  Wheel            w;
  w.init(t0);

  Conn c{1, t0 + HRTIME_SECONDS(5)};
  w.schedule(&c, c.deadline);

  Recorder rec;

  // Not yet due.
  CHECK(w.expire(t0 + HRTIME_SECONDS(4), 1000, rec) == 0);
  CHECK(rec.fired.empty());

  // Due.
  CHECK(w.expire(t0 + HRTIME_SECONDS(6), 1000, rec) == 1);
  REQUIRE(rec.fired.size() == 1);
  CHECK(rec.fired[0] == 1);

  // Fired elements leave the wheel; they do not fire twice.
  CHECK(w.expire(t0 + HRTIME_SECONDS(60), 1000, rec) == 0);
  CHECK(rec.fired.size() == 1);
}
