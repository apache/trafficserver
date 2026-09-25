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

#include "P_Net.h"
#include "iocore/eventsystem/Continuation.h"
#include "iocore/eventsystem/EThread.h"
#include "iocore/eventsystem/Event.h"
#include "iocore/eventsystem/Lock.h"
#include "iocore/eventsystem/VConnection.h"
#include "iocore/net/NetEvent.h"
#include "iocore/net/NetHandler.h"
#include "tscore/Ptr.h"
#include "tscore/ink_hrtime.h"
#include "tsutil/DbgCtl.h"
#include "tsutil/Metrics.h"
#include "ts/ats_probe.h"

#include <cinttypes>

// INKqa10496
// One Inactivity cop runs on each thread once every second and
// visits the NetEvents whose timer-wheel deadline is due, calling timeouts.
class InactivityCop : public Continuation
{
public:
  // A tick's worth of due elements is normally tiny; this is only a guard
  // against one correlated wave monopolizing the thread. Anything deferred
  // past the budget is picked up TIMEOUT_CONTINUE_DELAY later.
  static constexpr int TIMEOUT_BUDGET = 4096;

  // Deliberately not schedule_imm(): an immediate event is dispatched inline by
  // EThread::process_queue()'s own dequeue loop, so it would re-enter this pass
  // before the poll events run and bound nothing. A timed event lands in
  // EventQueue instead, where dequeue_ready() will not return it this loop
  // iteration, so the tail handler polls first.
  static constexpr ink_hrtime TIMEOUT_CONTINUE_DELAY = HRTIME_MSECONDS(1);

  InactivityCop(Ptr<ProxyMutex> const &m, NetHandler &nh) : Continuation(m.get()), _nh(nh)
  {
    SET_HANDLER(&InactivityCop::check_inactivity);
  }

  /// Fire callback for the timer wheel. Applies the timeout to each NetEvent
  /// whose true deadline has passed, preserving the semantics of the old sweep.
  struct Fire {
    NetHandler &nh;
    ink_hrtime  now;
    Event      *e;
    /// The wheel calls deadline_of() exactly once per element it pops, so this
    /// is an exact count of what the cop examined this run.
    int visited = 0;

    ink_hrtime
    deadline_of(NetEvent *ne)
    {
      ++visited;
      // Must agree exactly with rearm_timer(): closed NetEvents are reaped on
      // the next tick rather than at their original deadline.
      return ne->closed ? now : nh._earliest_deadline(ne);
    }

    void
    operator()(NetEvent *ne) const
    {
      // If we cannot get the lock don't stop just keep cleaning. The element
      // has already been popped from the wheel, so it must be rescheduled or
      // it will never be visited again.
      MUTEX_TRY_LOCK(lock, ne->get_mutex(), this_ethread());
      if (!lock.is_locked()) {
        Metrics::Counter::increment(net_rsb.inactivity_cop_lock_acquire_failure);
        nh.rearm_timer(ne);
        return;
      }

      if (ne->closed) {
        nh.free_netevent(ne);
        return;
      }

      // set a default inactivity timeout if one is not set
      // The event `EVENT_INACTIVITY_TIMEOUT` only be triggered if a read
      // or write I/O operation was set by `do_io_read()` or `do_io_write()`.
      if (ne->next_inactivity_timeout_at == 0 && ne->default_inactivity_timeout_in > 0 && (ne->read.enabled || ne->write.enabled)) {
        Dbg(dbg_ctl_inactivity_cop, "vc: %p inactivity timeout not set, setting a default of %d", ne,
            nh.config.default_inactivity_timeout);
        ne->use_default_inactivity_timeout = true;
        ne->next_inactivity_timeout_at     = now + ne->default_inactivity_timeout_in;
        ne->inactivity_timeout_in          = 0;
        ne->rearm_timer();
        Metrics::Counter::increment(net_rsb.default_inactivity_timeout_applied);
      }

      if (ne->next_inactivity_timeout_at && ne->next_inactivity_timeout_at < now) {
        if (ne->is_default_inactivity_timeout()) {
          // track the connections that timed out due to default inactivity
          Dbg(dbg_ctl_inactivity_cop, "vc: %p timed out due to default inactivity timeout", ne);
          Metrics::Counter::increment(net_rsb.default_inactivity_timeout_count);
        }
        if (nh.keep_alive_queue.in(ne)) {
          // only stat if the connection is in keep-alive, there can be other inactivity timeouts
          ink_hrtime diff = (now - (ne->next_inactivity_timeout_at - ne->inactivity_timeout_in)) / HRTIME_SECOND;
          Metrics::Counter::increment(net_rsb.keep_alive_queue_timeout_total, diff);
          Metrics::Counter::increment(net_rsb.keep_alive_queue_timeout_count);
        }
        Dbg(dbg_ctl_inactivity_cop_verbose, "ne: %p now: %" PRId64 " timeout at: %" PRId64 " timeout in: %" PRId64, ne,
            ink_hrtime_to_sec(now), ne->next_inactivity_timeout_at, ne->inactivity_timeout_in);
        ATS_PROBE6(net_inactivity_timeout, ne->get_fd(), now, ne->next_inactivity_timeout_at, ne->inactivity_timeout_in,
                   ne->is_default_inactivity_timeout() ? 1 : 0, ne->default_inactivity_timeout_in.load());
        ne->callback(VC_EVENT_INACTIVITY_TIMEOUT, e);
      } else if (ne->next_activity_timeout_at && ne->next_activity_timeout_at < now) {
        Dbg(dbg_ctl_inactivity_cop_verbose, "active ne: %p now: %" PRId64 " timeout at: %" PRId64 " timeout in: %" PRId64, ne,
            ink_hrtime_to_sec(now), ne->next_activity_timeout_at, ne->active_timeout_in);
        ne->callback(VC_EVENT_ACTIVE_TIMEOUT, e);
      } else {
        // Examined but not due. The wheel hands over an element once its
        // deadline is <= now while both tests above are strict <, so a deadline
        // exactly equal to now lands here. The element has already been popped,
        // so it has to go back in or nothing will ever visit it again and it
        // never times out. Re-arming rather than firing also matches the
        // pre-wheel sweep, which left such an element for the next tick.
        //
        // Only safe on this path: the callbacks above may free ne.
        nh.rearm_timer(ne);
      }
    }
  };

  int
  check_inactivity(int /* event */, Event *e)
  {
    if (run(ink_get_hrtime(), e) >= TIMEOUT_BUDGET) {
      // Deadlines are still due, so come back shortly instead of waiting out
      // the rest of the tick. The budget bounds how long one pass can hold the
      // poll thread; it is not a cap on how much work a tick may retire.
      // Waiting for the periodic event would be worse than it looks: expire()
      // does not advance its cursor until a bucket drains, so an over-full
      // bucket would also delay examining every tick behind it.
      this_ethread()->schedule_in(this, TIMEOUT_CONTINUE_DELAY);
    }
    return 0;
  }

  /** One timeout pass, with @a now supplied by the caller.
   *
   * Split out from the event handler purely so a test can drive the cop with a
   * controlled clock: the wheel's resolution is one second, so a harness that
   * advances real time cannot exercise a per-tick cost without sleeping. Every
   * deadline comparison in a pass uses this one timestamp.
   *
   * Scheduling policy stays in the handler, so driving this directly does not
   * queue events. Returns the number of timeouts fired; reaching
   * TIMEOUT_BUDGET means more are already due.
   */
  int
  run(ink_hrtime now, Event *e)
  {
    NetHandler &nh = _nh;

    Dbg(dbg_ctl_inactivity_cop_check, "Checking inactivity on Thread-ID #%d", this_ethread()->id);

    Fire      fire{nh, now, e};
    int const fired = nh.timer_wheel.expire(now, TIMEOUT_BUDGET, fire);

    // The cop's own cost had no telemetry before the wheel, which is how an
    // O(N)-per-second sweep regressed unnoticed.
    Metrics::Counter::increment(net_rsb.inactivity_cop_visited, fire.visited);
    if (fired >= TIMEOUT_BUDGET) {
      Metrics::Counter::increment(net_rsb.inactivity_cop_budget_exhausted);
    }

    // Only the keep-alive queue needs sweeping here: it is bounded by
    // max_connections_per_thread_in, and eviction is for capacity, not expiry.
    //
    // There is deliberately no manage_active_queue(nullptr, true) call. TS-4131
    // added one because the cop of that era only compared
    // next_inactivity_timeout_at and so never fired active timeouts; the forced
    // scan was the fallback that did. The wheel schedules on
    // _earliest_deadline(), the non-zero minimum of both deadlines, so an
    // expired active timeout now comes out of the wheel like any other and the
    // scan has nothing left to find - it was only re-walking the whole active
    // queue every second to duplicate work already done.
    nh.manage_keep_alive_queue();

    // Opt-in only: the check itself is the O(N) walk the wheel exists to avoid.
    if (dbg_ctl_inactivity_cop_audit.on() && ++_passes_since_audit >= AUDIT_INTERVAL) {
      _passes_since_audit = 0;
      audit(now);
    }

    return fired;
  }

  /** Walk every connection on the thread looking for ones the wheel will never
   * visit. Returns the number of anomalies found.
   *
   * This is the TS-4131 failure mode made detectable. That bug - the cop not
   * closing connections whose active timeout had expired - was worked around by
   * having the cop force a full scan of the active queue every tick. The wheel
   * removes the need for that scan, but "the cop silently stopped visiting a
   * connection" is exactly the class of bug this refactor hit repeatedly, so the
   * check survives as opt-in diagnostics instead of as a hot path.
   *
   * Only two states are anomalies:
   *
   *   - a live deadline with no wheel entry: nothing will ever visit it, so it
   *     can never time out;
   *   - a wheel entry whose cached deadline is *later* than the true one, which
   *     violates the never-later rule and fires late.
   *
   * Being merely overdue is not an anomaly: the cop legitimately lags when it
   * hits TIMEOUT_BUDGET. That case is reported separately, not counted.
   *
   * O(connections on the thread), so it is gated on the inactivity_cop_audit
   * debug tag and only runs every AUDIT_INTERVAL passes.
   */
  int
  audit(ink_hrtime now)
  {
    NetHandler &nh        = _nh;
    int         anomalies = 0;
    int         overdue   = 0;

    forl_LL(NetEvent, ne, nh.open_list)
    {
      ink_hrtime const deadline  = nh._earliest_deadline(ne);
      bool const       scheduled = nh.timer_wheel.is_scheduled(ne);

      if (deadline == 0) {
        continue; // no timeout wanted; not being scheduled is correct
      }

      if (!scheduled) {
        ++anomalies;
        Dbg(dbg_ctl_inactivity_cop_audit,
            "LOST ne: %p deadline: %" PRId64 " is live but not scheduled; active_queue: %d keep_alive_queue: %d", ne,
            ink_hrtime_to_sec(deadline), nh.active_queue.in(ne) ? 1 : 0, nh.keep_alive_queue.in(ne) ? 1 : 0);
        continue;
      }

      if (ne->timer_hook.deadline > deadline) {
        ++anomalies;
        Dbg(dbg_ctl_inactivity_cop_audit, "LATE ne: %p cached deadline %" PRId64 " is later than true deadline %" PRId64, ne,
            ink_hrtime_to_sec(ne->timer_hook.deadline), ink_hrtime_to_sec(deadline));
        continue;
      }

      if (deadline < now - AUDIT_SLACK) {
        ++overdue;
      }
    }

    Dbg(dbg_ctl_inactivity_cop_audit, "audit on Thread-ID #%d: %d anomalies, %d overdue but scheduled", this_ethread()->id,
        anomalies, overdue);
    return anomalies;
  }

private:
  NetHandler &_nh;

  // Passes between audits, and how far past its deadline a connection may be
  // before it is worth mentioning. The cop lags by up to a tick normally, and
  // further when it hits TIMEOUT_BUDGET, so the slack is deliberately loose.
  static constexpr int        AUDIT_INTERVAL = 60;
  static constexpr ink_hrtime AUDIT_SLACK    = 2 * TimerWheel<NetEvent>::TICK;

  int _passes_since_audit = 0;

  static inline DbgCtl dbg_ctl_inactivity_cop{"inactivity_cop"};
  static inline DbgCtl dbg_ctl_inactivity_cop_check{"inactivity_cop_check"};
  static inline DbgCtl dbg_ctl_inactivity_cop_verbose{"inactivity_cop_verbose"};
  static inline DbgCtl dbg_ctl_inactivity_cop_audit{"inactivity_cop_audit"};
};
