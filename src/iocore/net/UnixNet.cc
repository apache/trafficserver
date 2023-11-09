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

#include "iocore/net/AsyncSignalEventIO.h"
#include "P_Net.h"
#include "P_UnixNet.h"
#include "tscore/ink_hrtime.h"

#if TS_USE_LINUX_IO_URING
#include "iocore/io_uring/IO_URING.h"
#endif

ink_hrtime last_throttle_warning;
ink_hrtime last_shedding_warning;
int net_connections_throttle;
bool net_memory_throttle = false;
int fds_throttle;
ink_hrtime last_transient_accept_error;

NetHandler::Config NetHandler::global_config;
std::bitset<std::numeric_limits<unsigned int>::digits> NetHandler::active_thread_types;
const std::bitset<NetHandler::CONFIG_ITEM_COUNT> NetHandler::config_value_affects_per_thread_value{0x3};

namespace
{

DbgCtl dbg_ctl_inactivity_cop{"inactivity_cop"};
DbgCtl dbg_ctl_inactivity_cop_check{"inactivity_cop_check"};
DbgCtl dbg_ctl_inactivity_cop_verbose{"inactivity_cop_verbose"};

} // end anonymous namespace

NetHandler *
get_NetHandler(EThread *t)
{
  return static_cast<NetHandler *>(ETHREAD_GET_PTR(t, unix_netProcessor.netHandler_offset));
}

PollCont *
get_PollCont(EThread *t)
{
  return static_cast<PollCont *>(ETHREAD_GET_PTR(t, unix_netProcessor.pollCont_offset));
}

PollDescriptor *
get_PollDescriptor(EThread *t)
{
  PollCont *p = get_PollCont(t);
  return p->pollDescriptor;
}

// INKqa10496
// One Inactivity cop runs on each thread once every second and
// loops through the list of NetEvents and calls the timeouts
class InactivityCop : public Continuation
{
public:
  explicit InactivityCop(Ptr<ProxyMutex> &m) : Continuation(m.get()) { SET_HANDLER(&InactivityCop::check_inactivity); }
  int
  check_inactivity(int event, Event *e)
  {
    (void)event;
    ink_hrtime now = ink_get_hrtime();
    NetHandler &nh = *get_NetHandler(this_ethread());

    Dbg(dbg_ctl_inactivity_cop_check, "Checking inactivity on Thread-ID #%d", this_ethread()->id);
    // The rest NetEvents in cop_list which are not triggered between InactivityCop runs.
    // Use pop() to catch any closes caused by callbacks.
    while (NetEvent *ne = nh.cop_list.pop()) {
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
};

void
initialize_thread_for_net(EThread *thread)
{
  NetHandler *nh = get_NetHandler(thread);

  new (reinterpret_cast<ink_dummy_for_new *>(nh)) NetHandler();
  new (reinterpret_cast<ink_dummy_for_new *>(get_PollCont(thread))) PollCont(thread->mutex, nh);
  nh->mutex  = new_ProxyMutex();
  nh->thread = thread;

  PollCont *pc       = get_PollCont(thread);
  PollDescriptor *pd = pc->pollDescriptor;

  InactivityCop *inactivityCop = new InactivityCop(get_NetHandler(thread)->mutex);
  int cop_freq                 = 1;

  REC_ReadConfigInteger(cop_freq, "proxy.config.net.inactivity_check_frequency");
  memcpy(&nh->config, &NetHandler::global_config, sizeof(NetHandler::global_config));
  nh->configure_per_thread_values();
  thread->schedule_every(inactivityCop, HRTIME_SECONDS(cop_freq));

  thread->set_tail_handler(nh);

#if HAVE_EVENTFD
#if TS_USE_LINUX_IO_URING
  auto ep = new IOUringEventIO();
  ep->start(pd, IOUringContext::local_context());
#else
  auto ep = new AsyncSignalEventIO();
  ep->start(pd, thread->evfd, EVENTIO_READ);
#endif
#else
  auto ep = new AsyncSignalEventIO();
  ep->start(pd, thread->evpipe[0], EVENTIO_READ);
#endif
  thread->ep = ep;
}
