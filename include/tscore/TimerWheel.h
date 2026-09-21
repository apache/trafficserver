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

#pragma once

#include <cstdint>

#include "tscore/List.h"
#include "tscore/ink_hrtime.h"

// Per-element linkage for TimerWheel: the deadline the element was last
// scheduled with, plus which bucket (slot) it currently lives in, or -1
// if it is not in the wheel.
struct TimerWheelHook {
  ink_hrtime deadline = 0;
  int32_t    slot     = -1;
};

// An intrusive ring of DLL buckets, one per second (TICK), used to find
// elements whose deadline has passed without scanning every element.
//
// Not thread safe: each instance is owned and driven by a single thread.
//
// F (the callable passed to expire()) must provide:
//   ink_hrtime deadline_of(C *e) - the element's current true deadline;
//     0 means "no timeout", so the element is dropped from the wheel.
//   void operator()(C *e)        - called once per element whose true
//     deadline has passed; the element has already been removed from
//     the wheel by the time this is called.
//
// The scheduled deadline may be earlier than an element's eventual true
// deadline (it will simply be rearmed in place, lazily, at expire time)
// but must never be later, or the timeout fires late.
template <class C, class L = typename C::Link_timer_link> class TimerWheel
{
public:
  static constexpr int32_t    N_BUCKETS       = 1024;
  static constexpr ink_hrtime TICK            = HRTIME_SECOND;
  static constexpr int32_t    MAX_TICKS_AHEAD = N_BUCKETS - 1;

  void
  init(ink_hrtime now)
  {
    _cursor = now / TICK;
  }

  void
  schedule(C *e, ink_hrtime deadline)
  {
    cancel(e);
    e->timer_hook.deadline = deadline;
    _insert(e, _cursor + 1);
  }

  void
  cancel(C *e)
  {
    int32_t const slot = e->timer_hook.slot;

    if (slot >= 0) {
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
  expire(ink_hrtime now, int budget, F &&f)
  {
    if (budget <= 0) {
      return 0; // nothing to do; wheel state is left untouched
    }

    int64_t const now_tick = now / TICK;
    int           fired    = 0;

    while (_cursor < now_tick) {
      int64_t const tick   = _cursor + 1;
      DLL<C, L>    &bucket = _buckets[tick & (N_BUCKETS - 1)];

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
          return fired; // bucket keeps the rest; _cursor not advanced
        }
      }
      _cursor = tick; // only advance once fully drained
    }
    return fired;
  }

private:
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
  int64_t   _cursor = 0;
};
