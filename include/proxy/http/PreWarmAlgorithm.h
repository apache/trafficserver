/** @file

  Pre-Warming Pool Size Algorithm

  v1: periodical pre-warming only
  v2: periodical pre-warming + event based pre-warming

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

#include "tscore/ink_assert.h"
#include "tscore/ink_error.h"

#include <cstdint>
#include <algorithm>

namespace PreWarm
{
enum class Algorithm {
  V1 = 1,
  V2,
};

inline PreWarm::Algorithm
algorithm_version(int i)
{
  switch (i) {
  case 2:
    return PreWarm::Algorithm::V2;
  case 1:
    return PreWarm::Algorithm::V1;
  default:
    ink_abort("unsupported version v=%d", i);
  }
}

/**
   Periodical pre-warming for algorithm v1

   Expand the pool size to @requested_size

   @params min : min connections (configured)
   @params max : max connections (configured), -1 : unlimited

   @return how many connections needs to be pre-warmed for next period
 */
inline uint32_t
prewarm_size_v1_on_event_interval(uint32_t requested_size, uint32_t current_size, uint32_t min, int32_t max)
{
  uint32_t n = requested_size;

  // keep tunnel_min connections pre-warmed at least
  n = std::max(n, min);

  if (max >= 0) {
    n = std::min(n, static_cast<uint32_t>(max));
  }

  if (current_size >= n) {
    // we already have enough connections, don't need to open new connection
    return 0;
  } else {
    n -= current_size;
  }

  return n;
}

/**
   Periodical pre-warming for algorithm v2

   Expand the pool size to @current_size + @miss * @rate. The event based pre-warming handles the hit cases.

   @params min : min connections (configured)
   @params max : max connections (configured), -1 : unlimited

   @return how many connections needs to be pre-warmed for next period
 */
inline uint32_t
prewarm_size_v2_on_event_interval(uint32_t hit, uint32_t miss, uint32_t current_size, uint32_t min, int32_t max, double rate)
{
  if (hit + miss + current_size < min) {
    // fallback to v1 to keep min size
    return prewarm_size_v1_on_event_interval(hit + miss, current_size, min, max);
  }

  // Reached limit - do nothing
  if (max >= 0 && current_size >= static_cast<uint32_t>(max)) {
    return 0;
  }

  // Add #miss connections to the pool
  uint32_t n = miss * rate;

  // Check limit
  if (max >= 0 && n + current_size > static_cast<uint32_t>(max)) {
    ink_release_assert(static_cast<uint32_t>(max) > current_size);
    n = max - current_size;
  }

  return n;
}

} // namespace PreWarm
