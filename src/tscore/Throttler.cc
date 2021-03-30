/** @file

  Implement Throttler.

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

#include "tscore/Throttler.h"

Throttler::Throttler(std::chrono::microseconds interval) : _interval{interval} {}

bool
Throttler::is_throttled(uint64_t &skipped_count)
{
  TimePoint const now = Clock::now();
  TimePoint last_allowed_time{_last_allowed_time.load()};
  if ((last_allowed_time + _interval.load()) <= now) {
    if (_last_allowed_time.compare_exchange_strong(last_allowed_time, now)) {
      skipped_count     = _suppressed_count;
      _suppressed_count = 0;
      return false;
    }
  }
  ++_suppressed_count;
  return true;
}

uint64_t
Throttler::reset_counter()
{
  _last_allowed_time       = Clock::now();
  auto const skipped_count = _suppressed_count;
  _suppressed_count        = 0;
  return skipped_count;
}

void
Throttler::set_throttling_interval(std::chrono::microseconds new_interval)
{
  _interval = new_interval;
}
