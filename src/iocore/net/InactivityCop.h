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
#include "tscore/List.h"
#include "tscore/Ptr.h"
#include "tscore/ink_hrtime.h"
#include "tsutil/DbgCtl.h"
#include "tsutil/Metrics.h"
#include "ts/ats_probe.h"

#include <cinttypes>

// INKqa10496
// One Inactivity cop runs on each thread once every second and
// loops through the list of NetEvents and calls the timeouts
class InactivityCop : public Continuation
{
public:
  InactivityCop(Ptr<ProxyMutex> const &m, NetHandler &nh) : Continuation(m.get()), _nh(nh)
  {
    SET_HANDLER(&InactivityCop::check_inactivity);
  }

  int
  check_inactivity(int event, Event *e)
  {
    (void)event;
    ink_hrtime  now = ink_get_hrtime();
    NetHandler &nh  = _nh;

    Dbg(dbg_ctl_inactivity_cop_check, "Checking inactivity on Thread-ID #%d", this_ethread()->id);
    // The rest NetEvents in cop_list which are not triggered between InactivityCop runs.
    // Use pop() to catch any closes caused by callbacks.
    while (NetEvent *ne = nh.cop_list.pop()) {
      // These fields are written by the owning thread (this thread; open_list is
      // per-thread) and by plugin paths under ne's mutex, so a relaxed/unlocked read
      // here can be stale. That is fine: a stale "nothing to do" read cannot lose a
      // timeout permanently, since ne is pushed back onto cop_list from open_list
      // every run and will be re-examined next tick. Worst case is one extra tick of
      // delay, or one avoidable lock below that re-checks these same fields.
      ink_hrtime default_inactivity_timeout_in = ne->default_inactivity_timeout_in.load(std::memory_order_relaxed);
      bool       nothing_to_do =
        !ne->closed && default_inactivity_timeout_in != -1 &&
        !(ne->next_inactivity_timeout_at == 0 && default_inactivity_timeout_in > 0 && (ne->read.enabled || ne->write.enabled)) &&
        !(ne->next_inactivity_timeout_at && ne->next_inactivity_timeout_at < now) &&
        !(ne->next_activity_timeout_at && ne->next_activity_timeout_at < now);

      if (nothing_to_do) {
        continue;
      }

      // If we cannot get the lock don't stop just keep cleaning
      MUTEX_TRY_LOCK(lock, ne->get_mutex(), this_ethread());
      if (!lock.is_locked()) {
        Metrics::Counter::increment(net_rsb.inactivity_cop_lock_acquire_failure);
        continue;
      }

      if (ne->closed) {
        nh.free_netevent(ne);
        continue;
      }

      if (ne->default_inactivity_timeout_in == -1) {
        // If no context-specific default inactivity timeout has been set by an
        // override plugin, then use the global default.
        Dbg(dbg_ctl_inactivity_cop,
            "vc: %p setting the global default inactivity timeout of %d, next_inactivity_timeout_at: %" PRId64, ne,
            nh.config.default_inactivity_timeout, ne->next_inactivity_timeout_at);
        ne->set_default_inactivity_timeout(HRTIME_SECONDS(nh.config.default_inactivity_timeout));
      }

      // set a default inactivity timeout if one is not set
      // The event `EVENT_INACTIVITY_TIMEOUT` only be triggered if a read
      // or write I/O operation was set by `do_io_read()` or `do_io_write()`.
      if (ne->next_inactivity_timeout_at == 0 && ne->default_inactivity_timeout_in > 0 && (ne->read.enabled || ne->write.enabled)) {
        Dbg(dbg_ctl_inactivity_cop, "vc: %p inactivity timeout not set, setting a default of %d", ne,
            nh.config.default_inactivity_timeout);
        ne->use_default_inactivity_timeout = true;
        ne->next_inactivity_timeout_at     = ink_get_hrtime() + ne->default_inactivity_timeout_in;
        ne->inactivity_timeout_in          = 0;
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
      }
    }
    // The cop_list is empty now.
    // Let's reload the cop_list from open_list again.
    forl_LL(NetEvent, ne, nh.open_list)
    {
      if (ne->get_thread() == this_ethread()) {
        nh.cop_list.push(ne);
      }
    }
    // NetHandler will remove NetEvent from cop_list if it is triggered.
    // As the NetHandler runs, the number of NetEvents in the cop_list is decreasing.
    // NetHandler runs 100 times maximum between InactivityCop runs.
    // Therefore we don't have to check all the NetEvents as much as open_list.

    // Cleanup the active and keep-alive queues periodically
    nh.manage_active_queue(nullptr, true); // close any connections over the active timeout
    nh.manage_keep_alive_queue();

    return 0;
  }

private:
  NetHandler &_nh;

  static inline DbgCtl dbg_ctl_inactivity_cop{"inactivity_cop"};
  static inline DbgCtl dbg_ctl_inactivity_cop_check{"inactivity_cop_check"};
  static inline DbgCtl dbg_ctl_inactivity_cop_verbose{"inactivity_cop_verbose"};
};
