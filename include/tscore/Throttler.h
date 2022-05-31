/** @file

  A class for generic throttling.

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

#include <atomic>
#include <chrono>
#include <cstdint>

/** A class that exposes an interface for generic throttling of some action
 * against a certain interval.
 *
 * To use:
 *
 * 1. Create an instance of this class specifying the interval for which
 * something should be throttled. Alternatively, inherit from this class to
 * have the throttling interface apply to the object you want throttling for.
 *
 * 2. Prepend each decision for a given throttled action with a call to
 * is_throttled.
 *
 *   2a. If the is_throttled is false, then at least the configured number of
 *   microseconds has elapsed since the previous call in which is_throttled
 *   returned false. The number of times the check has been called between
 *   these two times is provided in the suppressed_count output parameter.
 *
 *   2b. If is_throttled returns true, then not enough time has elapsed
 *   since the last time the operation returned true per the throttling
 *   interval. Thus the operation should be skipped or suppressed, depending
 *   upon the context.
 *
 * For instance:
 *
 *    void foo()
 *    {
 *      using namespace std::chrono_literals;
 *      static Throttler t(300ms);
 *      uint64_t suppressed_count;
 *      if (!t.is_throttled(suppressed_count)) {
 *        std::printf("Alan bought another monitor\n");
 *        std::printf("We ignored Alan buying a monitor %llu times\n", suppressed_count);
 *      }
 *    }
 */
class Throttler
{
public:
  virtual ~Throttler() = default;

  /**
   * @param[in] interval The minimum number of microseconds between
   * calls to Throttler which should return true.
   */
  Throttler(std::chrono::microseconds interval);

  /** Whether the current event should be suppressed because the time since the
   * last unsuppressed event is less than the throttling interval.
   *
   * @param[out] suppressed_count If the return of this call is false (the action
   * should not be suppressed), this is populated with the approximate number
   * of suppressed events between the last unsuppressed event and the current
   * one.  Otherwise the value is not set. This value is approximate because,
   * if used in a multithreaded context, other threads may be querrying against
   * this function as well concurrently, and their count may not be applied
   * depending upon the timing of their query.
   *
   * @return True if the action is suppressed per the configured interval,
   * false otherwise.
   */
  virtual bool is_throttled(uint64_t &suppressed_count);

  /** Set the log throttling interval to a new value.
   *
   * @param[in] interval The new interval to set.
   */
  virtual void set_throttling_interval(std::chrono::microseconds new_interval);

  /** Manually reset the throttling counter to the current time.
   *
   * @return the number of messages skipped since the previous positive return
   * of the functor operator.
   */
  virtual uint64_t reset_counter();

private:
  /// Base clock.
  using Clock = std::chrono::system_clock;

  /** A time_point with a noexcept constructor.
   *
   * This is a workaround for older gcc and clang compilers which implemented an
   * older version of the standard which made atomic's noexcept construction
   * specification not compatible with time_point's undecorated constructor.
   */
  class TimePoint : public Clock::time_point
  {
  public:
    using time_point::time_point;

    // This noexcept specification makes TimePoint compatible with older
    // compiler implementations of atomic.
    constexpr TimePoint() noexcept : time_point() {}

    template <class Duration2> constexpr TimePoint(const time_point<Clock, Duration2> &t) : time_point(t) {}
  };

  /// Time that the last item was emitted.
  std::atomic<TimePoint> _last_allowed_time;

  /// The minimum number of microseconds desired between actions.
  std::atomic<std::chrono::microseconds> _interval{std::chrono::microseconds{0}};

  /// The number of calls to Throttler since the last
  uint64_t _suppressed_count = 0;
};
