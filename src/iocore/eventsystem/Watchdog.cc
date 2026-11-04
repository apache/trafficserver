/** @file

  A watchdog for event loops

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

#include "iocore/eventsystem/Watchdog.h"
#include "iocore/eventsystem/EThread.h"
#include "iocore/eventsystem/WatchdogPolicy.h"
#include "records/RecCore.h"
#include "records/RecDefs.h"
#include "tscore/Diags.h"
#include "tscore/ink_assert.h"
#include "tscore/ink_thread.h"
#include "tsutil/DbgCtl.h"
#include "tsutil/Metrics.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <functional>

namespace Watchdog
{

using ts::Metrics;

DbgCtl dbg_ctl_watchdog("watchdog");

namespace
{
  // this ensures `long long` duration for older compilers to match format specifiers
  using printable_micro = std::chrono::duration<long long, std::micro>;
  using printable_milli = std::chrono::duration<long long, std::milli>;

  /** Current value of accept_refuse(). Written only by the monitor thread and the config callback.

      Read from the accept paths on every accept loop.
   */
  std::atomic<bool> accept_refuse_state{false};

  /** Current value of proxy.config.exec_thread.watchdog.mode.

      File scope rather than a Monitor member on purpose: RecRegisterConfigUpdateCb has no
      unregister counterpart, so a callback that captured the Monitor would outlive it and fire
      against freed memory at shutdown. Keeping the mode here means the callback touches only static
      storage and remains valid for the life of the process.
   */
  std::atomic<Mode> current_mode{Mode::DISABLED};

  /** proxy.config.exec_thread.watchdog.timeout_refuse_ms - threshold for accept_refuse().

      Zero disables it entirely. Independent of the shrink timeout that drives the scan period: the
      accept paths stopping and the accept queues shrinking are separate decisions on the same peak,
      so this can be reloaded freely without disturbing the scan rate.
   */
  std::atomic<int64_t> refuse_timeout_ms{0};

  /// proxy.config.exec_thread.watchdog.backlog_min_permille - floor for backlog_scale_permille().
  std::atomic<int32_t> backlog_min_permille{50};

  /// Published listen backlog scale, in permille of the configured value. 1000 == unthrottled.
  std::atomic<int32_t> backlog_scale{1000};

  /** proxy.config.exec_thread.watchdog.backlog_decrease_permille - multiplicative decrease.

      Expressed as the permille of the *current* scale retained on each saturated scan, rather than
      as a divisor, so the whole range is reachable rather than just 1/2, 1/3, 1/4: 500 is the
      classic halving, 667 is a divide by 1.5, 900 is a slow squeeze, 975 is a crawl.

      Doubles as the release deadband - see release_threshold(). One number for how much margin is
      wanted against the overload, applied to the queue depth and to the signal alike.
   */
  std::atomic<int32_t> backlog_decrease_permille{500};

  /** proxy.config.exec_thread.watchdog.backlog_increase_permille - proportional increase.

      Permille of the scale *currently in force* offered as the step on each recovered scan, before
      the headroom attenuation in recovery_step_permille(). Equivalently, permille points of the
      configured backlog at full depth, which is where the two readings coincide and why one number
      serves as both the rate and the ceiling on the step - the scale never exceeds 1000, so the
      proportional value can never exceed the configured points.

      Proportional to the current depth rather than a flat number of points because what re-saturates
      the threads is the count of connections admitted, not the fraction of the baseline they
      represent, and a flat step is a fixed count whatever depth is in force. At the bottom of a hard
      shed that count is a multiple of the whole queue: at 100 points of a 65,535 baseline, the first
      recovered scan after a shed to 20 permille would readmit 6,553 connections onto a queue holding
      1,310. The same 100 here readmits 131, and the step grows with the queue as it climbs out.
   */
  std::atomic<int32_t> backlog_increase_permille{100};

  int
  update_watchdog_mode(const char * /* name */, RecDataT /* type */, RecData data, void * /* cookie */)
  {
    auto mode = static_cast<Mode>(data.rec_int);
    if (mode != Mode::DISABLED && mode != Mode::BACKPRESSURE) {
      Warning("Watchdog: ignoring out of range proxy.config.exec_thread.watchdog.mode %" PRId64, data.rec_int);
      return REC_ERR_OKAY;
    }

    Mode const prev = current_mode.exchange(mode, std::memory_order_relaxed);
    if (prev == mode) {
      return REC_ERR_OKAY;
    }

    // Drop any backpressure the outgoing mode had engaged. The monitor re-derives everything from
    // the new mode on its next scan, so the worst case is up to half a timeout of not shedding -
    // strictly the safe direction. Leaving it set would be the unsafe one: switching to mode 0
    // would otherwise strand the accept queues shrunk forever.
    //
    // The reporting gauges are left to the monitor rather than cleared here. It owns them, and
    // sync_state_gauges() corrects them on its next scan; writing them from this thread would race
    // its stores.
    if (mode == Mode::DISABLED) {
      accept_refuse_state.store(false, std::memory_order_relaxed);
      backlog_scale.store(1000, std::memory_order_relaxed);
    }

    Note("Watchdog: mode changed from %d to %d", static_cast<int>(prev), static_cast<int>(mode));
    return REC_ERR_OKAY;
  }

  int
  update_refuse_timeout(const char * /* name */, RecDataT /* type */, RecData data, void * /* cookie */)
  {
    int64_t const want = std::max<int64_t>(0, data.rec_int);
    refuse_timeout_ms.store(want, std::memory_order_relaxed);

    // Turning it off must resume accepting here rather than at the next scan - the whole point of
    // setting it to zero is to stop refusing immediately. Turning it on is left to the monitor, which
    // has an actual peak to compare against.
    if (want == 0) {
      accept_refuse_state.store(false, std::memory_order_relaxed);
    }
    return REC_ERR_OKAY;
  }

  int
  update_backlog_min_permille(const char * /* name */, RecDataT /* type */, RecData data, void * /* cookie */)
  {
    backlog_min_permille.store(static_cast<int32_t>(std::clamp<int64_t>(data.rec_int, 1, 1000)), std::memory_order_relaxed);
    return REC_ERR_OKAY;
  }

  int
  update_backlog_decrease_permille(const char * /* name */, RecDataT /* type */, RecData data, void * /* cookie */)
  {
    backlog_decrease_permille.store(static_cast<int32_t>(std::clamp<int64_t>(data.rec_int, 1, 999)), std::memory_order_relaxed);
    return REC_ERR_OKAY;
  }

  int
  update_backlog_increase_permille(const char * /* name */, RecDataT /* type */, RecData data, void * /* cookie */)
  {
    backlog_increase_permille.store(static_cast<int32_t>(std::clamp<int64_t>(data.rec_int, 1, 1000)), std::memory_order_relaxed);
    return REC_ERR_OKAY;
  }

} // namespace

void
register_config_callbacks()
{
  RecInt mode = RecGetRecordInt("proxy.config.exec_thread.watchdog.mode").value_or(0);
  current_mode.store(static_cast<Mode>(mode), std::memory_order_relaxed);
  RecRegisterConfigUpdateCb("proxy.config.exec_thread.watchdog.mode", update_watchdog_mode, nullptr);

  RecInt refuse_timeout = RecGetRecordInt("proxy.config.exec_thread.watchdog.timeout_refuse_ms").value_or(0);
  refuse_timeout_ms.store(std::max<int64_t>(0, refuse_timeout), std::memory_order_relaxed);
  RecRegisterConfigUpdateCb("proxy.config.exec_thread.watchdog.timeout_refuse_ms", update_refuse_timeout, nullptr);

  RecInt min_permille = RecGetRecordInt("proxy.config.exec_thread.watchdog.backlog_min_permille").value_or(50);
  backlog_min_permille.store(static_cast<int32_t>(std::clamp<int64_t>(min_permille, 1, 1000)), std::memory_order_relaxed);
  RecRegisterConfigUpdateCb("proxy.config.exec_thread.watchdog.backlog_min_permille", update_backlog_min_permille, nullptr);

  RecInt dec_permille = RecGetRecordInt("proxy.config.exec_thread.watchdog.backlog_decrease_permille").value_or(500);
  backlog_decrease_permille.store(static_cast<int32_t>(std::clamp<int64_t>(dec_permille, 1, 999)), std::memory_order_relaxed);
  RecRegisterConfigUpdateCb("proxy.config.exec_thread.watchdog.backlog_decrease_permille", update_backlog_decrease_permille,
                            nullptr);

  RecInt inc_permille = RecGetRecordInt("proxy.config.exec_thread.watchdog.backlog_increase_permille").value_or(100);
  backlog_increase_permille.store(static_cast<int32_t>(std::clamp<int64_t>(inc_permille, 1, 1000)), std::memory_order_relaxed);
  RecRegisterConfigUpdateCb("proxy.config.exec_thread.watchdog.backlog_increase_permille", update_backlog_increase_permille,
                            nullptr);
}

void
record_awake_span(Heartbeat &hb, std::chrono::steady_clock::duration span)
{
  auto ns = static_cast<uint64_t>(std::max<int64_t>(0, std::chrono::duration_cast<std::chrono::nanoseconds>(span).count()));
  // Only the owning EThread raises this; the monitor's exchange to zero is the only other writer, so a
  // plain load/store pair could lose a reset. Use CAS to keep the max semantics under that race.
  uint64_t prev = hb.peak_awake.load(std::memory_order_relaxed);
  while (prev < ns && !hb.peak_awake.compare_exchange_weak(prev, ns, std::memory_order_relaxed, std::memory_order_relaxed)) {
    ;
  }
}

bool
accept_refuse()
{
  // Gated on the live configuration so a scan that raced a config callback and published a stale
  // result cannot re-enable an action that has since been turned off.
  return accept_refuse_state.load(std::memory_order_relaxed) &&
         current_mode.load(std::memory_order_relaxed) == Mode::BACKPRESSURE &&
         refuse_timeout_ms.load(std::memory_order_relaxed) > 0;
}

int
backlog_scale_permille()
{
  if (current_mode.load(std::memory_order_relaxed) != Mode::BACKPRESSURE) {
    return 1000;
  }
  return backlog_scale.load(std::memory_order_relaxed);
}

SleepScope::SleepScope(Heartbeat &hb) : _hb(hb)
{
  auto now = std::chrono::steady_clock::now();

  // Publish the span that just ended, so a burst finishing between two monitor scans is not lost.
  auto last_wake = _hb.last_wake.load(std::memory_order_relaxed);
  if (last_wake != std::chrono::steady_clock::time_point::min()) {
    record_awake_span(_hb, now - last_wake);
  }

  // Enter the odd (sleeping) phase.
  _hb.seq.fetch_add(1, std::memory_order_relaxed);
}

SleepScope::~SleepScope()
{
  _hb.last_wake.store(std::chrono::steady_clock::now(), std::memory_order_relaxed);
  // Back to even (awake). Release so the monitor cannot observe the even sequence before the
  // timestamp store above.
  _hb.seq.fetch_add(1, std::memory_order_release);
}

Monitor::Monitor(EThread *threads[], size_t n_threads, std::chrono::milliseconds shrink_timeout_ms, bool legacy)
  : _threads(threads, threads + n_threads), _shrink_timeout{shrink_timeout_ms}, _legacy{legacy}
{
  // Precondition: shrink_timeout_ms must be > 0. A timeout of 0 indicates the watchdog is disabled
  // and the caller should not instantiate the Monitor (see traffic_server.cc).
  ink_release_assert(shrink_timeout_ms.count() > 0);
  if (!_legacy) {
    create_metrics();
  }
  _watchdog_thread = std::thread(std::bind_front(&Monitor::monitor_loop, this));
}

void
Monitor::create_metrics()
{
  _max_awake_ms          = Metrics::Gauge::createPtr("proxy.process.watchdog.max_awake_ms");
  _max_awake_ms_hwm      = Metrics::Gauge::createPtr("proxy.process.watchdog.max_awake_ms_hwm");
  _shrink_exceeded_count = Metrics::Counter::createPtr("proxy.process.watchdog.timeout_shrink_exceeded_count");
  _refuse_exceeded_count = Metrics::Counter::createPtr("proxy.process.watchdog.timeout_refuse_exceeded_count");
  _shrink_gauge          = Metrics::Gauge::createPtr("proxy.process.watchdog.accept_shrink");
  _shrink_count          = Metrics::Counter::createPtr("proxy.process.watchdog.accept_shrink_engaged_count");
  _refuse_gauge          = Metrics::Gauge::createPtr("proxy.process.watchdog.accept_refuse");
  _refuse_count          = Metrics::Counter::createPtr("proxy.process.watchdog.accept_refuse_engaged_count");
  _backlog_permille      = Metrics::Gauge::createPtr("proxy.process.watchdog.listen_backlog_permille");
  Metrics::Gauge::store(_backlog_permille, 1000);
}

Monitor::~Monitor()
{
  _shutdown.store(true, std::memory_order_release);
  _watchdog_thread.join();
  // Never leave the accept paths throttled behind a monitor that is no longer scanning.
  accept_refuse_state.store(false, std::memory_order_relaxed);
  backlog_scale.store(1000, std::memory_order_relaxed);
}

std::chrono::steady_clock::duration
Monitor::scan(std::chrono::steady_clock::time_point now)
{
  std::chrono::steady_clock::duration peak = std::chrono::steady_clock::duration::zero();

  for (size_t i = 0; i < _threads.size(); ++i) {
    EThread *t = _threads[i];

    // Completed spans the thread finished since the previous scan. Draining to zero here is what makes
    // this a per-scan peak rather than a lifetime high water mark.
    auto recorded = std::chrono::nanoseconds{t->heartbeat_state.peak_awake.exchange(0, std::memory_order_relaxed)};
    peak          = std::max<std::chrono::steady_clock::duration>(peak, recorded);

    // Seqlock read of the sleep/wake pair. An odd sequence means the thread is inside the blocking
    // poll, and a changed sequence means it transitioned while we were reading - either way the pair
    // is not a coherent snapshot and the live reading is skipped. peak_awake above already accounts
    // for that thread's completed work, so skipping loses nothing.
    uint64_t const seq_before = t->heartbeat_state.seq.load(std::memory_order_acquire);
    if (seq_before & 1) {
      continue; // sleeping
    }

    auto last_wake = t->heartbeat_state.last_wake.load(std::memory_order_relaxed);

    std::atomic_thread_fence(std::memory_order_acquire);
    if (t->heartbeat_state.seq.load(std::memory_order_relaxed) != seq_before) {
      continue; // torn - the thread slept or woke mid-read
    }

    if (last_wake == std::chrono::steady_clock::time_point::min()) {
      continue; // event loop hasn't completed its first poll
    }

    // Live snapshot of the span in progress. A thread stuck in a single long span never reaches the
    // top of its next loop to record itself in peak_awake, so it only shows up here.
    auto awake_duration = now - last_wake;
    if (awake_duration < std::chrono::steady_clock::duration::zero()) {
      // @a now is sampled once at the top of the scan, before this thread's seqlock read, so a thread
      // that woke in between can report a last_wake fractionally newer than it. Harmless and always
      // sub-millisecond, but it must not be fed to std::max as a duration.
      continue;
    }
    peak = std::max(peak, awake_duration);

    if (awake_duration > _shrink_timeout) {
      // Monitor thread is the sole reader (and warned_seq writer), so relaxed accesses are race-free.
      uint64_t warned_seq = t->heartbeat_state.warned_seq.load(std::memory_order_relaxed);
      if (warned_seq < seq_before) {
        if (_legacy) {
          // The deprecated timeout_ms warned here, and a config that still sets it gets what it asked for.
          Warning("Watchdog: [ET_NET %zu] has been awake for %lld ms", i,
                  std::chrono::duration_cast<printable_milli>(awake_duration).count());
        } else {
          // Debug rather than Warning: on a busy proxy the timeout is crossed routinely, and a log line
          // per event loop that runs long is noise an operator cannot act on. The condition is already
          // reported continuously and cheaply as proxy.process.watchdog.max_awake_ms, and counted per
          // scan as proxy.process.watchdog.timeout_shrink_exceeded_count, which is the right thing to
          // alert on.
          // This is here for the case where you need to know *which* thread.
          //
          // Reported once per loop iteration, not once per scan.
          Dbg(dbg_ctl_watchdog, "[ET_NET %zu] has been awake for %lld ms (shrink timeout %lld ms)", i,
              std::chrono::duration_cast<printable_milli>(awake_duration).count(),
              std::chrono::duration_cast<printable_milli>(_shrink_timeout).count());
        }
        t->heartbeat_state.warned_seq.store(seq_before, std::memory_order_relaxed);
      }
    }
  }

  return peak;
}

void
Monitor::sync_state_gauges()
{
  bool const refusing = accept_refuse_state.load(std::memory_order_relaxed);
  if (refusing != _refuse_reported) {
    _refuse_reported = refusing;
    Metrics::Gauge::store(_refuse_gauge, refusing ? 1 : 0);
  }
}

void
Monitor::monitor_loop()
{
  // Divide by a floating point 2 to avoid truncation to zero.
  auto sleep_time = _shrink_timeout / 2.0;
  ink_release_assert(sleep_time.count() > 0);
  Dbg(dbg_ctl_watchdog, "Starting watchdog with shrink timeout %lld ms, mode %d, on %zu threads.  sleep_time = %lld us",
      std::chrono::duration_cast<printable_milli>(_shrink_timeout).count(),
      static_cast<int>(current_mode.load(std::memory_order_relaxed)), _threads.size(),
      std::chrono::duration_cast<printable_micro>(sleep_time).count());

  ink_set_thread_name("[WATCHDOG]");

  while (!_shutdown.load(std::memory_order_acquire)) {
    auto peak = scan(std::chrono::steady_clock::now());

    if (_legacy) {
      std::this_thread::sleep_for(sleep_time);
      continue;
    }

    auto peak_ms = std::chrono::duration_cast<printable_milli>(peak).count();
    Metrics::Gauge::store(_max_awake_ms, peak_ms);

    // Monotonic high water mark alongside the per-scan gauge above. max_awake_ms covers only the
    // interval since the previous scan and is overwritten by the next one, so a spike that happens to
    // land between two scrapes of the metric is never seen at all - and the scan period is half the
    // shrink timeout, which is far shorter than any realistic scrape interval. This one only ever
    // rises, so the worst span the process has served is still readable after the fact.
    if (peak_ms > _hwm_ms) {
      _hwm_ms = peak_ms;
      Metrics::Gauge::store(_max_awake_ms_hwm, peak_ms);
    }

    // Re-read the mode every scan - it is reloadable, so it can change under us at any point.
    Mode const mode = current_mode.load(std::memory_order_relaxed);

    // The arithmetic lives in advance() so it can be exercised without a running monitor. This loop gathers
    // its inputs, applies the result to the published atomics and metrics, and reports the edges. The refuse
    // state is read back from the published atomic rather than mirrored in a member, because
    // update_refuse_timeout() and update_watchdog_mode() also clear it - a local mirror would go stale against
    // those and suppress the store that re-engages.
    PolicyConfig config;
    config.backpressure      = mode == Mode::BACKPRESSURE;
    config.shrink_timeout    = _shrink_timeout;
    config.refuse_timeout    = std::chrono::milliseconds{refuse_timeout_ms.load(std::memory_order_relaxed)};
    config.decrease_permille = backlog_decrease_permille.load(std::memory_order_relaxed);
    config.increase_permille = backlog_increase_permille.load(std::memory_order_relaxed);
    config.min_permille      = backlog_min_permille.load(std::memory_order_relaxed);

    PolicyState const  before{_shrink_engaged, accept_refuse_state.load(std::memory_order_relaxed), _backlog_scale, _trouble_scale};
    auto const         result = advance(config, before, peak);
    PolicyState const &after  = result.state;

    // Counted per saturated scan and deliberately independent of @c mode, so mode 0 - report only -
    // still gets a monotonic record of how often the threads crossed the shrink threshold. The engaged
    // counters below only move on a state edge, which says nothing about how long or how often, and
    // the max_awake_ms gauge can miss the crossing entirely between scrapes. Dividing this by the scan
    // rate gives the fraction of time spent over the threshold. The refuse threshold is counted the same way.
    if (result.saturated) {
      Metrics::Counter::increment(_shrink_exceeded_count);
    }
    if (result.refuse_exceeded) {
      Metrics::Counter::increment(_refuse_exceeded_count);
    }

    _shrink_engaged = after.shrink_engaged;
    _trouble_scale  = after.trouble_scale;

    if (after.shrink_engaged != before.shrink_engaged) {
      Metrics::Gauge::store(_shrink_gauge, after.shrink_engaged ? 1 : 0);
      if (after.shrink_engaged) {
        Metrics::Counter::increment(_shrink_count);
      }
      Dbg(dbg_ctl_watchdog, "backlog shrink -> %s - peak awake %lld ms, shrink timeout %lld ms",
          after.shrink_engaged ? "on" : "off", peak_ms, std::chrono::duration_cast<printable_milli>(_shrink_timeout).count());
    }

    // Also compare against the published value: a config callback resets it without touching
    // _backlog_scale, and the two must be reconciled even when the monitor's own scale is unchanged.
    if (after.scale != before.scale || after.scale != backlog_scale.load(std::memory_order_relaxed)) {
      _backlog_scale = after.scale;
      backlog_scale.store(after.scale, std::memory_order_relaxed);
      Metrics::Gauge::store(_backlog_permille, after.scale);
      Dbg(dbg_ctl_watchdog, "listen backlog scale -> %d permille - peak awake %lld ms", after.scale, peak_ms);
    }

    if (after.refuse != before.refuse) {
      accept_refuse_state.store(after.refuse, std::memory_order_relaxed);
      if (after.refuse) {
        Metrics::Counter::increment(_refuse_count);
      }
      Dbg(dbg_ctl_watchdog, "accept refuse -> %s - peak awake %lld ms, refuse timeout %lld ms", after.refuse ? "on" : "off",
          peak_ms, static_cast<long long>(config.refuse_timeout.count()));
    }

    sync_state_gauges();

    std::this_thread::sleep_for(sleep_time);
  }

  // A legacy monitor never created the metrics below, so the null pointers must not reach a store.
  if (_legacy) {
    Dbg(dbg_ctl_watchdog, "Stopping watchdog");
    return;
  }

  if (accept_refuse_state.load(std::memory_order_relaxed) || _backlog_scale != 1000) {
    accept_refuse_state.store(false, std::memory_order_relaxed);
    backlog_scale.store(1000, std::memory_order_relaxed);
    Metrics::Gauge::store(_shrink_gauge, 0);
    Metrics::Gauge::store(_backlog_permille, 1000);
  }
  sync_state_gauges();
  Dbg(dbg_ctl_watchdog, "Stopping watchdog");
}
} // namespace Watchdog
