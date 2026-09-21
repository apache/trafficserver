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

#include <algorithm>
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

TEST_CASE("TimerWheel relinks an element whose deadline moved out", "[libts][TimerWheel]")
{
  ink_hrtime const t0 = HRTIME_SECONDS(1000);
  Wheel            w;
  w.init(t0);

  Conn c{1, t0 + HRTIME_SECONDS(5)};
  w.schedule(&c, c.deadline);

  // Activity extends the true deadline without touching the wheel - this is
  // the whole point of the design.
  c.deadline = t0 + HRTIME_SECONDS(20);

  Recorder rec;
  CHECK(w.expire(t0 + HRTIME_SECONDS(6), 1000, rec) == 0);
  CHECK(rec.fired.empty());
  CHECK(w.is_scheduled(&c));

  CHECK(w.expire(t0 + HRTIME_SECONDS(21), 1000, rec) == 1);
  CHECK(rec.fired.size() == 1);
}

TEST_CASE("TimerWheel drops an element whose timeout was disabled", "[libts][TimerWheel]")
{
  ink_hrtime const t0 = HRTIME_SECONDS(1000);
  Wheel            w;
  w.init(t0);

  Conn c{1, t0 + HRTIME_SECONDS(5)};
  w.schedule(&c, c.deadline);
  c.deadline = 0; // no timeout

  Recorder rec;
  CHECK(w.expire(t0 + HRTIME_SECONDS(6), 1000, rec) == 0);
  CHECK_FALSE(w.is_scheduled(&c));
}

TEST_CASE("TimerWheel cancel removes an element", "[libts][TimerWheel]")
{
  ink_hrtime const t0 = HRTIME_SECONDS(1000);
  Wheel            w;
  w.init(t0);

  Conn c{1, t0 + HRTIME_SECONDS(5)};
  w.schedule(&c, c.deadline);
  CHECK(w.is_scheduled(&c));
  w.cancel(&c);
  CHECK_FALSE(w.is_scheduled(&c));

  Recorder rec;
  CHECK(w.expire(t0 + HRTIME_SECONDS(60), 1000, rec) == 0);
}

// Rescheduling must move the element, not leave a stale second linkage.
// Integration depends on this: shortening a timeout re-arms via schedule().
TEST_CASE("TimerWheel reschedule moves an already-scheduled element", "[libts][TimerWheel]")
{
  ink_hrtime const t0 = HRTIME_SECONDS(1000);
  Wheel            w;
  w.init(t0);

  Conn c{1, t0 + HRTIME_SECONDS(500)};
  w.schedule(&c, c.deadline);

  // Shorten it.
  c.deadline = t0 + HRTIME_SECONDS(3);
  w.schedule(&c, c.deadline);

  Recorder rec;
  CHECK(w.expire(t0 + HRTIME_SECONDS(4), 1000, rec) == 1);
  REQUIRE(rec.fired.size() == 1);
  CHECK(rec.fired[0] == 1);

  // It must not still be linked somewhere for the original deadline.
  CHECK(w.expire(t0 + HRTIME_SECONDS(600), 1000, rec) == 0);
  CHECK(rec.fired.size() == 1);
}

// A deadline past the wheel's range is clamped and must be re-inserted on
// arrival rather than firing early or aliasing onto the bucket being drained.
TEST_CASE("TimerWheel handles deadlines beyond its range", "[libts][TimerWheel]")
{
  ink_hrtime const t0  = HRTIME_SECONDS(1000);
  int const        far = Wheel::N_BUCKETS * 3;
  Wheel            w;
  w.init(t0);

  Conn c{1, t0 + HRTIME_SECONDS(far)};
  w.schedule(&c, c.deadline);

  Recorder rec;
  // Walk right up to the deadline one partial range at a time; it must never
  // fire early and must always still be scheduled.
  for (int s = 1; s < far; s += Wheel::N_BUCKETS / 2) {
    CHECK(w.expire(t0 + HRTIME_SECONDS(s), 1000, rec) == 0);
    CHECK(w.is_scheduled(&c));
  }
  CHECK(rec.fired.empty());

  CHECK(w.expire(t0 + HRTIME_SECONDS(far + 1), 1000, rec) == 1);
  CHECK(rec.fired.size() == 1);
}

TEST_CASE("TimerWheel budget defers the remainder to the next call", "[libts][TimerWheel]")
{
  ink_hrtime const t0 = HRTIME_SECONDS(1000);
  Wheel            w;
  w.init(t0);

  std::vector<Conn> conns;
  conns.reserve(10);
  for (int i = 0; i < 10; ++i) {
    conns.emplace_back(i, t0 + HRTIME_SECONDS(5));
  }
  for (auto &c : conns) {
    w.schedule(&c, c.deadline);
  }

  Recorder         rec;
  ink_hrtime const now = t0 + HRTIME_SECONDS(6);

  CHECK(w.expire(now, 4, rec) == 4);
  CHECK(rec.fired.size() == 4);
  CHECK(w.expire(now, 4, rec) == 4);
  CHECK(rec.fired.size() == 8);
  CHECK(w.expire(now, 4, rec) == 2);
  CHECK(rec.fired.size() == 10);
  CHECK(w.expire(now, 4, rec) == 0);
}

// Many connections sharing one timeout value is the real traffic pattern:
// deadlines cluster on a handful of config values.
TEST_CASE("TimerWheel fires a large clustered population exactly once each", "[libts][TimerWheel]")
{
  ink_hrtime const t0 = HRTIME_SECONDS(1000);
  int const        n  = 10000;
  Wheel            w;
  w.init(t0);

  std::vector<Conn> conns;
  conns.reserve(n);
  for (int i = 0; i < n; ++i) {
    conns.emplace_back(i, t0 + HRTIME_SECONDS(30 + (i % 4)));
  }
  for (auto &c : conns) {
    w.schedule(&c, c.deadline);
  }

  Recorder rec;
  for (int s = 1; s <= 40; ++s) {
    w.expire(t0 + HRTIME_SECONDS(s), 100000, rec);
  }

  REQUIRE(rec.fired.size() == static_cast<size_t>(n));
  std::vector<int> seen = rec.fired;
  std::sort(seen.begin(), seen.end());
  seen.erase(std::unique(seen.begin(), seen.end()), seen.end());
  CHECK(seen.size() == static_cast<size_t>(n)); // no element fired twice
}

// A non-positive budget must fire nothing and leave the wheel untouched;
// callers that compute a remaining budget dynamically may legitimately hit 0.
TEST_CASE("TimerWheel expire with a non-positive budget does nothing", "[libts][TimerWheel]")
{
  ink_hrtime const t0 = HRTIME_SECONDS(1000);
  Wheel            w;
  w.init(t0);

  Conn c{1, t0 + HRTIME_SECONDS(5)};
  w.schedule(&c, c.deadline);

  Recorder rec;
  CHECK(w.expire(t0 + HRTIME_SECONDS(6), 0, rec) == 0);
  CHECK(rec.fired.empty());
  CHECK(w.is_scheduled(&c));

  CHECK(w.expire(t0 + HRTIME_SECONDS(6), -1, rec) == 0);
  CHECK(rec.fired.empty());
  CHECK(w.is_scheduled(&c));

  CHECK(w.expire(t0 + HRTIME_SECONDS(6), 1, rec) == 1);
  CHECK(rec.fired.size() == 1);
}
