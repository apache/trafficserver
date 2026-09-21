/** @file

  A timer wheel: O(1) amortized scheduling and expiry of per-element
  deadlines, used to replace O(N)-per-tick scans of open connections.

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

#pragma once

#include <algorithm>
#include <cstdint>

#include "tscore/List.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_hrtime.h"

// Per-element linkage for TimerWheel: the deadline the element was last
// scheduled with, which bucket (slot) it currently lives in (-1 if not in
// the wheel), and which wheel instance owns it.
//
// An element must never be scheduled on more than one wheel at a time.
// Moving an element between wheels (e.g. a NetVConnection migrating between
// per-thread wheels) requires calling cancel() on the old wheel before
// calling schedule() on the new one; owner is asserted in cancel() and
// schedule() to catch a missed hand-off, which would otherwise splice two
// unrelated rings together.
//
// Destroying an element while it is still scheduled is a use-after-free:
// the element's destructor has no way to reach the wheel and unlink it.
// Callers must cancel() before destruction.
struct TimerWheelHook {
  ink_hrtime  deadline = 0;
  int32_t     slot     = -1;
  void const *owner    = nullptr;
};

// An intrusive ring of DLL buckets, one per second (TICK), used to find
// elements whose deadline has passed without scanning every element.
//
// Not thread safe: each instance is owned and driven by a single thread.
//
// F (the callable passed to expire()) must provide, callable through a
// non-const F &, and may hold mutable state shared across the calls that
// expire() makes on a single pass:
//   ink_hrtime deadline_of(C *e) - the element's current true deadline;
//     0 means "no timeout", so the element is dropped from the wheel.
//   void operator()(C *e)        - called once per element whose true
//     deadline has passed; the element has already been removed from
//     the wheel by the time this is called. The callback may call
//     schedule()/cancel() on this wheel, including re-scheduling the very
//     element being fired; the wheel guards against that landing back in
//     the bucket currently being drained.
//
// The scheduled deadline may be earlier than an element's eventual true
// deadline (it will simply be rearmed in place, lazily, at expire time)
// but must never be later, or the timeout fires late.
//
// expire()'s return value equal to budget means the caller is behind and
// should call again promptly. If the driving thread lags by more than
// N_BUCKETS ticks (e.g. init() was never called, or a long stall), the
// cursor is fast-forwarded rather than replaying every intervening empty
// tick; elements that would have been visited during that lag lose strict
// firing-order fidelity relative to each other, but none fire early or are
// lost.
// N_BUCKETS default: sized from src/tscore/unit_tests/test_TimerWheel.cc's
// "TimerWheel bucket-count sizing sweep" benchmark. At the 30s/120s keepalive
// timeouts (the dominant real case) 256 through 4096 are all already
// steady-state (~1.0 visits per element per timeout period); the difference
// only shows up for timeouts beyond the ring's range (e.g. a multi-hour
// tunnel active timeout), which get revisited every Buckets-1 ticks until
// due. Measured steady state for a 4h timeout: 1024 buckets costs ~14.9
// revisits per element per period, 4096 costs ~4. The memory difference
// (8 KiB vs 32 KiB per wheel, one wheel per ET_NET thread) is negligible
// either way, so 4096 is strictly better and is the default.
template <class C, int32_t Buckets = 4096, class L = typename C::Link_timer_link> class TimerWheel
{
public:
  static constexpr int32_t    N_BUCKETS       = Buckets;
  static constexpr ink_hrtime TICK            = HRTIME_SECOND;
  static constexpr int32_t    MAX_TICKS_AHEAD = N_BUCKETS - 1;

  static_assert((N_BUCKETS & (N_BUCKETS - 1)) == 0, "N_BUCKETS must be a power of two");

  void
  init(ink_hrtime now)
  {
    _cursor = now / TICK;
  }

  void
  schedule(C *e, ink_hrtime deadline)
  {
    cancel(e);
    e->timer_hook.owner    = this;
    e->timer_hook.deadline = deadline;
    // Floor past both the last fully-drained tick and, if we are being
    // called reentrantly from inside expire()'s fire callback, past the
    // bucket currently being drained - otherwise a reschedule from within
    // the callback could land back in that bucket and be popped again in
    // the same pass.
    _insert(e, std::max(_cursor + 1, _draining_tick + 1));
  }

  void
  cancel(C *e)
  {
    int32_t const slot = e->timer_hook.slot;

    if (slot >= 0) {
      ink_assert(e->timer_hook.owner == this);
      _buckets[slot].remove(e);
      e->timer_hook.slot = -1;
    }
  }

  bool
  is_scheduled(C const *e) const
  {
    return e->timer_hook.slot >= 0;
  }

  template <typename F>
  int
  expire(ink_hrtime now, int budget, F &f)
  {
    if (budget <= 0) {
      return 0; // nothing to do; wheel state is left untouched
    }

    int64_t const now_tick = now / TICK;

    // A cursor lagging by more than a full ring has already had every
    // bucket aliased over at least once; fast-forward instead of replaying
    // N_BUCKETS empty ticks (this also covers expire() being called before
    // init(), where _cursor starts at 0).
    if (now_tick - _cursor > N_BUCKETS) {
      _cursor = now_tick - N_BUCKETS;
    }

    int fired = 0;

    while (_cursor < now_tick) {
      int64_t const tick   = _cursor + 1;
      DLL<C, L>    &bucket = _buckets[tick & (N_BUCKETS - 1)];

      {
        DrainGuard const guard(_draining_tick, tick);

        while (C *e = bucket.pop()) {
          e->timer_hook.slot = -1;

          ink_hrtime const deadline = f.deadline_of(e);

          if (deadline == 0) {
            continue; // no timeout: drop out of the wheel
          }
          if (deadline > now) {
            e->timer_hook.deadline = deadline;
            _insert(e, tick + 1); // lazy rearm; floor tick+1 avoids re-entering `tick`
            continue;
          }

          f(e);
          ++fired;
          if (fired >= budget) {
            return fired; // guard resets _draining_tick; bucket keeps the rest; _cursor not advanced
          }
        }
      }
      _cursor = tick; // only advance once fully drained
    }
    return fired;
  }

private:
  // Marks _draining_tick for the lifetime of one bucket's drain pass so
  // that a reentrant schedule() (called from the fire callback) cannot
  // land back in that bucket. Resets on every exit, including the budget
  // early-return above.
  class DrainGuard
  {
  public:
    DrainGuard(int64_t &draining_tick, int64_t tick) : _draining_tick(draining_tick) { _draining_tick = tick; }
    ~DrainGuard() { _draining_tick = INT64_MIN; }

    DrainGuard(DrainGuard const &)            = delete;
    DrainGuard &operator=(DrainGuard const &) = delete;

  private:
    int64_t &_draining_tick;
  };

  void
  _insert(C *e, int64_t floor_tick)
  {
    int64_t tick = e->timer_hook.deadline / TICK;

    if (tick < floor_tick) {
      tick = floor_tick;
    } else if (tick > floor_tick + MAX_TICKS_AHEAD - 1) {
      tick = floor_tick + MAX_TICKS_AHEAD - 1;
    }

    auto const slot    = static_cast<int32_t>(tick & (N_BUCKETS - 1));
    e->timer_hook.slot = slot;
    _buckets[slot].push(e);
  }

  DLL<C, L> _buckets[N_BUCKETS];
  int64_t   _cursor        = 0;
  int64_t   _draining_tick = INT64_MIN; // sentinel: no bucket currently being drained
};
