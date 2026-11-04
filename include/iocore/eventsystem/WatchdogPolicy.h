/** @file

  The decision arithmetic of the watchdog's accept backpressure, free of metrics, records and threads.

  `Watchdog::Monitor` feeds each scan's peak awake span through advance() and applies the result. Keeping
  the arithmetic here lets it be exercised directly, without a running monitor.

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
#include <chrono>
#include <cstdint>

namespace Watchdog
{

/// Most aggressive release deadband the retain knob will produce. See release_threshold().
constexpr int32_t min_release_permille = 500;

/// The configuration one scan is decided against.
struct PolicyConfig {
  /// Whether proxy.config.exec_thread.watchdog.mode is backpressure. Nothing engages otherwise.
  bool backpressure = false;
  /// proxy.config.exec_thread.watchdog.timeout_shrink_ms
  std::chrono::milliseconds shrink_timeout{0};
  /// proxy.config.exec_thread.watchdog.timeout_refuse_ms; zero disables refuse.
  std::chrono::milliseconds refuse_timeout{0};
  /// proxy.config.exec_thread.watchdog.backlog_decrease_permille
  int decrease_permille = 500;
  /// proxy.config.exec_thread.watchdog.backlog_increase_permille
  int increase_permille = 100;
  /// proxy.config.exec_thread.watchdog.backlog_min_permille
  int min_permille = 50;
};

/// What carries from one scan to the next.
struct PolicyState {
  /// Whether the accept queue shrink is engaged. Latched through the release deadband.
  bool shrink_engaged = false;
  /// Whether the accept paths are refusing. Latched through the release deadband.
  bool refuse = false;
  /// Listen backlog scale in permille. 1000 is unthrottled.
  int scale = 1000;
  /** Backlog scale in force when the current shrink episode began, in permille; 0 when none.

      The mark that headroom-attenuated recovery decelerates into. Cleared once the scale is back to 1000.
   */
  int trouble_scale = 0;

  bool operator==(PolicyState const &) const = default;
};

/// The outcome of one scan.
struct PolicyResult {
  PolicyState state;
  /// The peak reached the shrink threshold, whatever the mode.
  bool saturated = false;
  /// The peak reached the refuse threshold, whatever the mode. Never set while that threshold is zero.
  bool refuse_exceeded = false;
};

/** Peak awake span at or below which a scan counts as recovered, given an action @a threshold.

    Every action engages at its own threshold but releases only here, so the two differ and the
    band between them holds the current state - a Schmitt trigger rather than a bare comparison.
    Without it a peak one millisecond under the threshold reads as full recovery when it is really
    the boundary, which is exactly where the state chatters and where releasing lets the queue climb
    back into the next step of load.

    Derived from the decrease knob rather than from one of its own because it is the same statement:
    retain 500 means halve the queue when saturated *and* wait for the peak to halve before believing
    the recovery. Aggressive shedding and late release belong together.

    Floored at @c min_release_permille so the coupling cannot make backpressure effectively
    unreleasable - at retain 1 the threshold would be two milliseconds on a two second timeout,
    which a merely busy server may never reach. Half the threshold is already far outside
    measurement noise, so nothing is lost by refusing to go deeper; below the floor the knob keeps
    sharpening the shed and stops affecting the release.
 */
inline std::chrono::nanoseconds
release_threshold(std::chrono::milliseconds threshold, int decrease_permille)
{
  int32_t const retain = std::max<int32_t>(min_release_permille, decrease_permille);
  // Scaled in nanoseconds so a small timeout keeps its fractional-millisecond boundary.
  return std::chrono::duration_cast<std::chrono::nanoseconds>(threshold) * retain / 1000;
}

/** @return Permille points to add to the backlog scale on a recovered scan.

    @a increase_permille of the scale currently in force, attenuated by the headroom remaining to
    @c state.trouble_scale, or unattenuated when there is no mark to measure against or the scale is
    already above it.
 */
inline int
recovery_step_permille(PolicyState const &state, int increase_permille)
{
  int const rate = increase_permille;

  // Proportional to the depth currently in force, so the step is a small number of connections while
  // the queue is deep in the hole and grows as it climbs out - the absolute count admitted per scan
  // is what re-saturates the threads, and a flat step in points of the baseline is the same count at
  // every depth. The cap restates the invariant rather than binding: the scale is at most 1000, so
  // the proportional value is at most the configured points, which it reaches only at full depth.
  // The floor keeps the step positive against integer truncation - below a scale of 1000/rate the
  // product truncates to zero, and a zero step would leave the queue stranded at that depth, because
  // the caller only republishes when the value actually changes.
  int const step = std::max(1, std::min(state.scale * rate / 1000, rate));

  // No mark yet - the first recovery of the process, or one that already climbed back to full depth.
  // Nothing to attenuate against, so the proportional step stands.
  if (state.trouble_scale <= 0) {
    return step;
  }

  // Past the mark already - it has been proven survivable since, so it carries no information about
  // the rest of the climb. Full rate the rest of the way to 1000.
  if (state.scale >= state.trouble_scale) {
    return step;
  }

  // Within one full step of the mark: land exactly on it rather than tapering into it. The attenuated
  // step goes to zero as the gap closes, and the one point floor above would otherwise turn the last
  // stretch into a crawl of single-point scans - dozens of them at a large backlog - for no benefit,
  // since the mark is a depth to sit at and reassess, not one to creep up on. Never returns more than
  // @a step, because the branch condition is exactly that the remaining gap fits inside one, and @a
  // step is itself bounded by the configured rate - so making the step proportional cannot turn this
  // into an unbounded leap onto the mark.
  if (state.scale + step >= state.trouble_scale) {
    return state.trouble_scale - state.scale;
  }

  // Attenuate by the fraction of the way still to go, so the climb decelerates as it approaches the
  // depth that last caused trouble instead of arriving there at full speed. Far below the mark this is
  // ~1 and costs nothing. The floor keeps the step positive against integer truncation; the snap above
  // is what stops it from being reached repeatedly.
  int const headroom = state.trouble_scale - state.scale;
  return std::max(1, step * headroom / state.trouble_scale);
}

/** Decide one scan.

    Each action engages on the first scan at or above its own threshold and releases only once the
    peak has fallen to the deadband below it. Between the two thresholds the current state holds. Shrink
    and refuse each have their own threshold on the same peak and neither depends on the other.

    @param[in] config The configuration in force for this scan.
    @param[in] state The state the previous scan left.
    @param[in] peak The largest awake span across all watched threads since the previous scan.
 */
inline PolicyResult
advance(PolicyConfig const &config, PolicyState state, std::chrono::steady_clock::duration peak)
{
  PolicyResult result;

  result.saturated     = peak >= config.shrink_timeout;
  bool const recovered = peak <= release_threshold(config.shrink_timeout, config.decrease_permille);

  // Shrink stage state. Mode off releases now rather than waiting out the deadband - the point of
  // turning it off is to stop throttling immediately.
  bool const was_shrinking = state.shrink_engaged;
  if (!config.backpressure) {
    state.shrink_engaged = false;
  } else if (result.saturated) {
    state.shrink_engaged = true;
  } else if (recovered) {
    state.shrink_engaged = false;
  }

  // The depth still in force as this episode begins is, by construction, a depth that let in more
  // than the threads could serve. Remember it as the mark that headroom-attenuated recovery slows
  // down against. Captured before the decrease below touches the scale.
  if (state.shrink_engaged && !was_shrinking) {
    state.trouble_scale = state.scale;
  }

  // Accept queue sizing. Multiplicative decrease on a saturated scan, proportional increase once
  // released, and held steady inside the release deadband - a peak that has come down but not far
  // enough is not evidence for shedding harder, and without the saturated term a long stay in the
  // deadband would squeeze the queue toward the floor while it was recovering.
  if (config.backpressure) {
    int want = state.scale;

    if (state.shrink_engaged && result.saturated) {
      // Integer truncation would stall the decrease entirely at small scales - 3 permille retained
      // at 900 rounds back to 3 - so always give up at least one point while shrinking.
      int const next = std::min(state.scale * config.decrease_permille / 1000, state.scale - 1);
      want           = std::max(config.min_permille, next);
    } else if (!state.shrink_engaged) {
      want = std::min(1000, state.scale + recovery_step_permille(state, config.increase_permille));
    }
    state.scale = want;

    // Fully recovered - forget the mark so the next episode measures its own trouble depth rather
    // than throttling recovery against a stale one.
    if (state.scale >= 1000) {
      state.trouble_scale = 0;
    }
  } else if (state.scale != 1000) {
    // Mode turned off while the queues were reduced.
    state.scale         = 1000;
    state.trouble_scale = 0;
  }

  // Refuse stage: stop draining the listen sockets entirely.
  result.refuse_exceeded = config.refuse_timeout.count() > 0 && peak >= config.refuse_timeout;
  if (!config.backpressure || config.refuse_timeout.count() == 0) {
    state.refuse = false;
  } else if (result.refuse_exceeded) {
    state.refuse = true;
  } else if (peak <= release_threshold(config.refuse_timeout, config.decrease_permille)) {
    state.refuse = false;
  }

  result.state = state;
  return result;
}

} // namespace Watchdog
