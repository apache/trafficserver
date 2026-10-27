/** @file

  A watchdog for event loops

  Each event thread advertises its current state through a lightweight
  "heartbeat" struct: the thread publishes the timestamps for the most recent
  sleep/wake pair along with a monotonically increasing sequence number.
  `Watchdog::Monitor`, started from `traffic_server.cc`, runs in its own
  `std::thread` and periodically scans those heartbeats (the scan period comes from
  `proxy.config.exec_thread.watchdog.timeout_shrink_ms`, where 0 disables the monitor).
  The monitor never touches event-system locks, keeping the runtime overhead in
  the hot loop confined to a handful of atomic updates.

  A thread awake past `timeout_shrink_ms` is reported under the `watchdog` debug tag rather
  than as a warning: on a busy proxy the threshold is crossed routinely, and a log line per
  long event loop is noise an operator cannot act on.  The condition is published
  continuously as `proxy.process.watchdog.max_awake_ms` and counted as
  `proxy.process.watchdog.timeout_shrink_exceeded_count`, which is what to alert on; the
  debug output exists for when you need to know *which* thread.

  Each event loop also records the length of the awake span it just finished in
  `Heartbeat::peak_awake`, keeping the largest span seen since the monitor last
  drained that field.  On every scan the monitor takes the larger of that
  recorded peak and a live snapshot of any thread that is awake right now, so a
  burst that both started and ended between two scans is still accounted for.
  The largest value across all watched threads is published as
  `proxy.process.watchdog.max_awake_ms`.

  That gauge covers only the interval since the previous scan, so it is overwritten twice per scan
  period and a spike can fall entirely between two scrapes of the metric.  Two monotonic companions
  exist for that reason: `proxy.process.watchdog.max_awake_ms_hwm`, the largest peak the process has
  ever published, and `proxy.process.watchdog.timeout_shrink_exceeded_count`, incremented on every scan
  whose peak reached `timeout_shrink_ms` - with `timeout_refuse_exceeded_count` doing the same for the
  refuse threshold.  All are maintained in every mode, including mode 0, and none can be missed by a
  slow scrape.

  `proxy.config.exec_thread.watchdog.mode` selects whether anything is done about it, and is
  reloadable at runtime.  Mode 0 only reports.  Mode 1 applies inbound backpressure through two
  *independent* thresholds on the same peak, each with its own action - there is no escalation over
  time and no ordering between them, so either can be used alone:

  - `proxy.config.exec_thread.watchdog.timeout_shrink_ms` - once the peak reaches this, the accept
    queues are progressively *shrunk* and the kernel sheds the excess on our behalf - see
    @c backlog_scale_permille.  Accepting continues throughout, because a bounded queue only drains
    as fast as ATS calls `accept4`, which makes the kernel admit new connections at approximately the
    rate ATS can actually serve.  That is a closed loop with no set point to calibrate, and it costs nothing per
    connection.  This threshold also fixes the monitor's scan period at half its value, which is why
    it is the one that requires a restart.

  - `proxy.config.exec_thread.watchdog.timeout_refuse_ms` - once the peak reaches this, the accept
    paths stop taking connections off their listen sockets altogether (@c accept_refuse).  Queued
    connections cost nothing at all, and a client picked up once the threads recover sees no error
    whatsoever.  Zero, the default, disables it; the scan period does not depend on it, so it is
    freely reloadable.

  Both can be engaged at once, in which case the queue is bounded *and* not being drained.  That is
  the reason to normally set `timeout_refuse_ms` above `timeout_shrink_ms`: the cheap, self-limiting
  control acts first, and the blunt one only at a depth of saturation where handing the queue to the
  kernel is no longer enough.

  Not draining is only free while the queue can hold what arrives.  ATS's drain rate on a refusing
  listener is exactly zero, so the accept queue can only fill: it absorbs roughly
  `arrival_rate x time_above_timeout_refuse_ms` connections before the kernel starts dropping new
  SYNs at the queue's own depth, and that depth may already have been reduced by the shrink
  threshold.  Set `timeout_refuse_ms` high enough that reaching it really is worse than not
  accepting, and expect the kernel to do the shedding past that point rather than the queue to absorb
  it indefinitely.

  Backpressure applies to a whole listen socket, which is what allows refusal to be free - the
  decision is taken before `accept4`, so nothing is spent on a connection that is left queued.  The
  cost is that a per-connection exemption is impossible: a peer address is not knowable until after
  the connection has been accepted.  Local connections are protected instead by giving loopback its
  own listen socket - see `start_loopback_companion_acceptor` in HttpProxyServerMain.cc - and marking
  only that acceptor exempt via @c AcceptOptions::f_watchdog_exempt.

  Neither action refuses anything.  On Linux a full accept queue silently drops the SYN - no SYN-ACK,
  no RST - so a shed client's `connect` hangs while it retransmits with exponential backoff (1s, 2s,
  4s, ...), and it either fails on its own connect timeout or gets in late once the queue drains.
  `net.ipv4.tcp_abort_on_overflow` does not change that: it only covers the race where the queue
  filled between the SYN and the client's final ACK.  Re-listening with a smaller backlog only lowers
  the limit; connections already queued past it are accepted normally.  The SYN queue limit is derived
  from the same backlog, so a small floor can also trigger SYN cookies without a real flood.

  Kernel-side drops are invisible to ATS: watch the listen-queue overflow counters (ListenOverflows,
  ListenDrops) in `netstat -s` alongside `proxy.process.net.watchdog_listen_backlog`.

  Each threshold engages on the first scan whose peak reaches it, and releases only once the peak has
  fallen to a deadband below it - `proxy.config.exec_thread.watchdog.backlog_decrease_permille` of
  that threshold, floored at half.  Two thresholds rather than one because a peak a millisecond under
  the mark is not a recovery, it is the boundary: the state chatters there, and treating it as clear
  lets the queue climb back during a lull and re-admit the flood just as the next step in load
  arrives.  Between the two the current state holds, neither shedding harder nor releasing.

  Sharing the retain knob rather than adding a threshold of its own is deliberate.  Both are the same
  statement about how much margin is wanted against the overload - halve the queue when saturated, and
  wait for the peak to halve before believing it is over - and the alternative is a second rate to tune
  against the first.

  The increase is proportional to the depth currently in force -
  `proxy.config.exec_thread.watchdog.backlog_increase_permille` of it per recovered scan - so the
  number of connections readmitted per scan is small while the queue is deep in the hole and grows
  along with it.  What re-saturates the threads is the count admitted, not the fraction of the
  baseline it represents, and a step in points of the baseline is the same count at every depth: at
  the bottom of a hard shed it is a multiple of the entire queue.  Because the scale never exceeds
  1000, that one number is both the rate and the ceiling on the step, which it reaches only at full
  depth.

  The step is further attenuated by the headroom remaining to the depth that was in force when the
  episode began, so the climb decelerates as it approaches that mark - the queue returns to service
  through the range already known to be safe without probing the depth that last caused trouble
  between two scans.  The last step onto the mark is taken whole rather than tapered, so the approach
  lands on it in bounded time instead of creeping the final points one at a time.  Once above it the
  mark is stale information and the unattenuated rate resumes.

  `timeout_shrink_ms` is not reloadable: it fixes the monitor's scan period, so changing it requires
  a restart.  The mode, refuse timeout, backlog floor, and decrease and increase rates are.

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
#include <vector>
#include <thread>

#include "tsutil/Metrics.h"

class EThread;

namespace Watchdog
{
/// Behavior when the peak awake span reaches one of the watchdog's action thresholds.
enum class Mode : int {
  DISABLED     = 0, ///< Report only - warnings and metrics, no behavior change.
  BACKPRESSURE = 1, ///< Shrink the accept queues, and stop draining them, while the threads are saturated.
};

struct Heartbeat {
  std::atomic<std::chrono::time_point<std::chrono::steady_clock>> last_wake{
    std::chrono::steady_clock::time_point::min()}; // set right after waking from sleep (e.g. epoll_wait returns)
  /** Seqlock over @c last_wake, and loop counter for warning dedup.

      Odd means the thread is inside the blocking poll; even means it is awake.  The monitor uses
      this to read @c last_wake without tearing: it samples @c seq before and after reading the
      timestamp and discards the sample if the value changed or was odd.
   */
  std::atomic<uint64_t> seq{0};
  std::atomic<uint64_t> warned_seq{0}; // last seq we logged a warning about
  /** Longest completed awake span, in nanoseconds, since the monitor last drained this field.

      Written by the owning EThread at the top of each loop with the span it just finished, read and
      reset to zero by the monitor.  This is what lets a burst that starts and ends entirely between
      two scans still show up in @c proxy.process.watchdog.max_awake_ms.
   */
  std::atomic<uint64_t> peak_awake{0};
};

/** Record a completed awake span, keeping the largest seen so far.

    Called by the owning EThread, so the only contending access is the monitor's exchange to zero.
 */
void record_awake_span(Heartbeat &hb, std::chrono::steady_clock::duration span);

/** Brackets the blocking poll inside a @c LoopTailHandler::waitForActivity implementation.

    "Awake" means everything the event loop does other than block waiting for I/O.  Most of
    @c waitForActivity is *not* blocking - it processes the enabled list, dispatches every ready
    descriptor, and drains the ready list - so bracketing the whole call would attribute the
    single largest component of a busy loop to sleep.  Handlers construct this around their
    @c do_poll (or equivalent) and nothing else.

    A handler that does not use this simply leaves its thread's heartbeat at the initial sentinel,
    and the monitor skips that thread rather than reporting something wrong.
 */
class SleepScope
{
public:
  explicit SleepScope(Heartbeat &hb);
  ~SleepScope();

  SleepScope(SleepScope const &)            = delete;
  SleepScope &operator=(SleepScope const &) = delete;

private:
  Heartbeat &_hb;
};

/** Whether the accept paths should leave the connections queued on their listen sockets.

    True while the peak awake span has reached @c proxy.config.exec_thread.watchdog.timeout_refuse_ms,
    released once it falls to the deadband below that (see the file comment).  Cheap enough (one
    relaxed load) to call from the accept loops.

    Always false when the refuse timeout is zero (the default), when the monitor is not running in
    @c Mode::BACKPRESSURE, or when it is not running at all - an unscanned signal must never be left
    latched on.

    Independent of @c backlog_scale_permille(): the two thresholds are separate, so accepts can be
    stopped with the queue at full depth, or the queue shrunk while accepts continue, or both.
 */
bool accept_refuse();

/** Permille of the configured listen backlog the accept queues should currently be sized to.

    @c 1000 means unthrottled - the value from @c proxy.config.net.listen_backlog.  Lower values are
    published while the peak awake span has reached
    @c proxy.config.exec_thread.watchdog.timeout_shrink_ms, decreasing multiplicatively per saturated
    scan (@c proxy.config.exec_thread.watchdog.backlog_decrease_permille) and recovering
    proportionally once it clears (@c proxy.config.exec_thread.watchdog.backlog_increase_permille of
    the depth in force, attenuated by the headroom remaining to the depth that began the episode),
    floored at @c proxy.config.exec_thread.watchdog.backlog_min_permille.

    Permille rather than percent because the interesting range is compressed near the bottom: on a
    large backlog the difference between 1% and 2% is thousands of connections, and percent cannot
    express anything between them.  It also keeps the multiplicative decrease from truncating to a
    standstill at small scales.

    Rate controlled rather than a step because the accept queue is shared with the kernel's own
    connection-level backoff: every scan spent over-admitted is a scan of damage, while jumping
    straight back to the full depth re-admits the flood and re-saturates.  Fast down and slow up is
    the safe asymmetry, and both directions are relative to the depth in force so the pacing does not
    change meaning with the size of the baseline.

    Held steady inside the release deadband, where a peak that has come down but not far enough is
    grounds for neither direction.
 */
int backlog_scale_permille();

/** Register the dynamic update callbacks for the reloadable watchdog settings.

    Call once at startup, before the Monitor is constructed.  Safe to call even when the watchdog is
    disabled - the values are tracked either way, they simply have no effect without a running
    Monitor.
 */
void register_config_callbacks();

class Monitor
{
public:
  /** @param legacy Started from the deprecated @c proxy.config.exec_thread.watchdog.timeout_ms rather than
      @c timeout_shrink_ms.  The monitor then behaves exactly as that setting did: it logs a warning for each
      event loop iteration that runs past the timeout and does nothing else - no metrics and no
      backpressure, whatever the other watchdog settings say.
   */
  explicit Monitor(EThread *threads[], size_t n_threads, std::chrono::milliseconds shrink_timeout_ms, bool legacy = false);
  ~Monitor();
  Monitor() = delete;

private:
  const std::vector<EThread *> _threads;
  std::thread                  _watchdog_thread;
  /** @c proxy.config.exec_thread.watchdog.timeout_shrink_ms.

      Both the threshold the accept queue shrink engages at and, at half its value, the scan period -
      which is why it cannot be reloaded.
   */
  const std::chrono::milliseconds _shrink_timeout;
  /// Started from the deprecated timeout_ms - warn only. See the constructor.
  const bool                      _legacy;
  std::atomic<bool>               _shutdown     = false;
  ts::Metrics::Gauge::AtomicType *_max_awake_ms = nullptr;
  /// Lifetime high water mark of @c _max_awake_ms, so a spike between two scrapes is not lost.
  ts::Metrics::Gauge::AtomicType *_max_awake_ms_hwm = nullptr;
  /// Scans whose peak reached the shrink threshold, counted in every mode.
  ts::Metrics::Counter::AtomicType *_shrink_exceeded_count = nullptr;
  /// Scans whose peak reached the refuse threshold, counted in every mode. Never moves while that
  /// threshold is zero.
  ts::Metrics::Counter::AtomicType *_refuse_exceeded_count = nullptr;
  ts::Metrics::Gauge::AtomicType   *_shrink_gauge          = nullptr;
  ts::Metrics::Counter::AtomicType *_shrink_count          = nullptr;
  ts::Metrics::Gauge::AtomicType   *_refuse_gauge          = nullptr;
  ts::Metrics::Counter::AtomicType *_refuse_count          = nullptr;
  ts::Metrics::Gauge::AtomicType   *_backlog_permille      = nullptr;
  /// Whether the accept queue shrink is currently engaged. Latched through the release deadband.
  bool _shrink_engaged = false;
  /// Largest peak published so far, in ms. Mirrors @c _max_awake_ms_hwm to keep the store off the hot path.
  int64_t _hwm_ms = 0;
  /// Current listen backlog scale, in permille. See backlog_scale_permille().
  int _backlog_scale = 1000;
  /** Backlog scale in force when the current shrink episode began, in permille; 0 when none.

      The depth that admitted more than the threads could serve, and therefore the mark that
      headroom-attenuated recovery decelerates into. Cleared once the scale is back to 1000, so each
      episode is paced against its own history rather than a stale one.
   */
  int _trouble_scale = 0;
  /** Last value written to @c _refuse_gauge.

      The config callbacks clear the published state directly, without touching the gauge, so the
      monitor cannot rely on seeing the edge itself. It compares this against the published state
      each scan and rewrites a gauge that has gone stale.
   */
  bool _refuse_reported = false;

  /// Bring @c _refuse_gauge in line with the published state.
  void sync_state_gauges();

  void create_metrics();
  void monitor_loop();
  /// @return The peak awake span across all watched threads since the previous scan.
  std::chrono::steady_clock::duration scan(std::chrono::steady_clock::time_point now);
  /** @return Permille points to add to the backlog scale on this recovered scan.

      @c proxy.config.exec_thread.watchdog.backlog_increase_permille of the scale currently in force,
      attenuated by the headroom remaining to @c _trouble_scale, or unattenuated when there is no mark
      to measure against or the scale is already above it.
   */
  int recovery_step_permille() const;
};

} // namespace Watchdog
