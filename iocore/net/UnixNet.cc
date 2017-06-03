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

#include "P_Net.h"

ink_hrtime last_throttle_warning;
ink_hrtime last_shedding_warning;
ink_hrtime emergency_throttle_time;
int net_connections_throttle;
bool net_memory_throttle = false;
int fds_throttle;
int fds_limit = 8000;
ink_hrtime last_transient_accept_error;

extern "C" void fd_reify(struct ev_loop *);

// INKqa10496
// One Inactivity cop runs on each thread once every second and
// loops through the list of NetVCs and calls the timeouts
class InactivityCop : public Continuation
{
public:
  explicit InactivityCop(Ptr<ProxyMutex> &m) : Continuation(m.get()) { SET_HANDLER(&InactivityCop::check_inactivity); }
  int
  check_inactivity(int event, Event *e)
  {
    (void)event;
    ink_hrtime now = Thread::get_hrtime();
    NetHandler &nh = *get_NetHandler(this_ethread());

    Debug("inactivity_cop_check", "Checking inactivity on Thread-ID #%d", this_ethread()->id);
    // The rest NetVCs in cop_list which are not triggered between InactivityCop runs.
    // Use pop() to catch any closes caused by callbacks.
    while (UnixNetVConnection *vc = nh.cop_list.pop()) {
      // If we cannot get the lock don't stop just keep cleaning
      MUTEX_TRY_LOCK(lock, vc->mutex, this_ethread());
      if (!lock.is_locked()) {
        NET_INCREMENT_DYN_STAT(inactivity_cop_lock_acquire_failure_stat);
        continue;
      }

      if (vc->closed) {
        close_UnixNetVConnection(vc, e->ethread);
        continue;
      }

      if (vc->next_inactivity_timeout_at && vc->next_inactivity_timeout_at < now) {
        if (nh.keep_alive_queue.in(vc)) {
          // only stat if the connection is in keep-alive, there can be other inactivity timeouts
          ink_hrtime diff = (now - (vc->next_inactivity_timeout_at - vc->inactivity_timeout_in)) / HRTIME_SECOND;
          NET_SUM_DYN_STAT(keep_alive_queue_timeout_total_stat, diff);
          NET_INCREMENT_DYN_STAT(keep_alive_queue_timeout_count_stat);
        }
        Debug("inactivity_cop_verbose", "vc: %p now: %" PRId64 " timeout at: %" PRId64 " timeout in: %" PRId64, vc,
              ink_hrtime_to_sec(now), vc->next_inactivity_timeout_at, vc->inactivity_timeout_in);
        vc->handleEvent(EVENT_IMMEDIATE, e);
      }
    }
    // The cop_list is empty now.
    // Let's reload the cop_list from open_list again.
    forl_LL(UnixNetVConnection, vc, nh.open_list)
    {
      if (vc->thread == this_ethread()) {
        nh.cop_list.push(vc);
      }
    }
    // NetHandler will remove NetVC from cop_list if it is triggered.
    // As the NetHandler runs, the number of NetVCs in the cop_list is decreasing.
    // NetHandler runs 100 times maximum between InactivityCop runs.
    // Therefore we don't have to check all the NetVCs as much as open_list.

    // Cleanup the active and keep-alive queues periodically
    nh.manage_active_queue(true); // close any connections over the active timeout
    nh.manage_keep_alive_queue();

    return 0;
  }
};

PollCont::PollCont(Ptr<ProxyMutex> &m, int pt)
  : Continuation(m.get()), net_handler(nullptr), nextPollDescriptor(nullptr), poll_timeout(pt)
{
  pollDescriptor = new PollDescriptor();
  SET_HANDLER(&PollCont::pollEvent);
}

PollCont::PollCont(Ptr<ProxyMutex> &m, NetHandler *nh, int pt)
  : Continuation(m.get()), net_handler(nh), nextPollDescriptor(nullptr), poll_timeout(pt)
{
  pollDescriptor = new PollDescriptor();
  SET_HANDLER(&PollCont::pollEvent);
}

PollCont::~PollCont()
{
  delete pollDescriptor;
  if (nextPollDescriptor != nullptr) {
    delete nextPollDescriptor;
  }
}

//
// PollCont continuation which does the epoll_wait
// and stores the resultant events in ePoll_Triggered_Events
//
int
PollCont::pollEvent(int event, Event *e)
{
  (void)event;
  (void)e;

  if (likely(net_handler)) {
    /* checking to see whether there are connections on the ready_queue (either read or write) that need processing [ebalsa] */
    if (likely(!net_handler->read_ready_list.empty() || !net_handler->write_ready_list.empty() ||
               !net_handler->read_enable_list.empty() || !net_handler->write_enable_list.empty())) {
      NetDebug("iocore_net_poll", "rrq: %d, wrq: %d, rel: %d, wel: %d", net_handler->read_ready_list.empty(),
               net_handler->write_ready_list.empty(), net_handler->read_enable_list.empty(),
               net_handler->write_enable_list.empty());
      poll_timeout = 0; // poll immediately returns -- we have triggered stuff to process right now
    } else {
      poll_timeout = net_config_poll_timeout;
    }
  }
// wait for fd's to tigger, or don't wait if timeout is 0
#if TS_USE_EPOLL
  pollDescriptor->result =
    epoll_wait(pollDescriptor->epoll_fd, pollDescriptor->ePoll_Triggered_Events, POLL_DESCRIPTOR_SIZE, poll_timeout);
  NetDebug("iocore_net_poll", "[PollCont::pollEvent] epoll_fd: %d, timeout: %d, results: %d", pollDescriptor->epoll_fd,
           poll_timeout, pollDescriptor->result);
#elif TS_USE_KQUEUE
  struct timespec tv;
  tv.tv_sec  = poll_timeout / 1000;
  tv.tv_nsec = 1000000 * (poll_timeout % 1000);
  pollDescriptor->result =
    kevent(pollDescriptor->kqueue_fd, nullptr, 0, pollDescriptor->kq_Triggered_Events, POLL_DESCRIPTOR_SIZE, &tv);
  NetDebug("iocore_net_poll", "[PollCont::pollEvent] kqueue_fd: %d, timeout: %d, results: %d", pollDescriptor->kqueue_fd,
           poll_timeout, pollDescriptor->result);
#elif TS_USE_PORT
  int retval;
  timespec_t ptimeout;
  ptimeout.tv_sec  = poll_timeout / 1000;
  ptimeout.tv_nsec = 1000000 * (poll_timeout % 1000);
  unsigned nget    = 1;
  if ((retval = port_getn(pollDescriptor->port_fd, pollDescriptor->Port_Triggered_Events, POLL_DESCRIPTOR_SIZE, &nget, &ptimeout)) <
      0) {
    pollDescriptor->result = 0;
    switch (errno) {
    case EINTR:
    case EAGAIN:
    case ETIME:
      if (nget > 0) {
        pollDescriptor->result = (int)nget;
      }
      break;
    default:
      ink_assert(!"unhandled port_getn() case:");
      break;
    }
  } else {
    pollDescriptor->result = (int)nget;
  }
  NetDebug("iocore_net_poll", "[PollCont::pollEvent] %d[%s]=port_getn(%d,%p,%d,%d,%d),results(%d)", retval,
           retval < 0 ? strerror(errno) : "ok", pollDescriptor->port_fd, pollDescriptor->Port_Triggered_Events,
           POLL_DESCRIPTOR_SIZE, nget, poll_timeout, pollDescriptor->result);
#else
#error port me
#endif
  return EVENT_CONT;
}

static void
net_signal_hook_callback(EThread *thread)
{
#if HAVE_EVENTFD
  uint64_t counter;
  ATS_UNUSED_RETURN(read(thread->evfd, &counter, sizeof(uint64_t)));
#elif TS_USE_PORT
/* Nothing to drain or do */
#else
  char dummy[1024];
  ATS_UNUSED_RETURN(read(thread->evpipe[0], &dummy[0], 1024));
#endif
}

static void
net_signal_hook_function(EThread *thread)
{
#if HAVE_EVENTFD
  uint64_t counter = 1;
  ATS_UNUSED_RETURN(write(thread->evfd, &counter, sizeof(uint64_t)));
#elif TS_USE_PORT
  PollDescriptor *pd = get_PollDescriptor(thread);
  ATS_UNUSED_RETURN(port_send(pd->port_fd, 0, thread->ep));
#else
  char dummy = 1;
  ATS_UNUSED_RETURN(write(thread->evpipe[1], &dummy, 1));
#endif
}

void
initialize_thread_for_net(EThread *thread)
{
  new ((ink_dummy_for_new *)get_NetHandler(thread)) NetHandler();
  new ((ink_dummy_for_new *)get_PollCont(thread)) PollCont(thread->mutex, get_NetHandler(thread));
  get_NetHandler(thread)->mutex = new_ProxyMutex();
  PollCont *pc                  = get_PollCont(thread);
  PollDescriptor *pd            = pc->pollDescriptor;

  thread->schedule_imm(get_NetHandler(thread));

  InactivityCop *inactivityCop = new InactivityCop(get_NetHandler(thread)->mutex);
  int cop_freq                 = 1;

  REC_ReadConfigInteger(cop_freq, "proxy.config.net.inactivity_check_frequency");
  thread->schedule_every(inactivityCop, HRTIME_SECONDS(cop_freq));

  thread->signal_hook = net_signal_hook_function;
  thread->ep          = (EventIO *)ats_malloc(sizeof(EventIO));
  thread->ep->type    = EVENTIO_ASYNC_SIGNAL;
#if HAVE_EVENTFD
  thread->ep->start(pd, thread->evfd, nullptr, EVENTIO_READ);
#else
  thread->ep->start(pd, thread->evpipe[0], nullptr, EVENTIO_READ);
#endif
}

// NetHandler method definitions

NetHandler::NetHandler()
  : Continuation(nullptr),
    trigger_event(nullptr),
    keep_alive_queue_size(0),
    active_queue_size(0),
    max_connections_per_thread_in(0),
    max_connections_active_per_thread_in(0),
    max_connections_in(0),
    max_connections_active_in(0),
    inactive_threashold_in(0),
    transaction_no_activity_timeout_in(0),
    keep_alive_no_activity_timeout_in(0),
    default_inactivity_timeout(0)
{
  SET_HANDLER((NetContHandler)&NetHandler::startNetEvent);
}

int
update_nethandler_config(const char *name, RecDataT data_type ATS_UNUSED, RecData data, void *cookie)
{
  NetHandler *nh = static_cast<NetHandler *>(cookie);
  ink_assert(nh != nullptr);
  bool update_per_thread_configuration = false;

  if (nh != nullptr) {
    if (strcmp(name, "proxy.config.net.max_connections_in") == 0) {
      Debug("net_queue", "proxy.config.net.max_connections_in updated to %" PRId64, data.rec_int);
      nh->max_connections_in          = data.rec_int;
      update_per_thread_configuration = true;
    }
    if (strcmp(name, "proxy.config.net.max_active_connections_in") == 0) {
      Debug("net_queue", "proxy.config.net.max_active_connections_in updated to %" PRId64, data.rec_int);
      nh->max_connections_active_in   = data.rec_int;
      update_per_thread_configuration = true;
    }
    if (strcmp(name, "proxy.config.net.inactive_threashold_in") == 0) {
      Debug("net_queue", "proxy.config.net.inactive_threashold_in updated to %" PRId64, data.rec_int);
      nh->inactive_threashold_in = data.rec_int;
    }
    if (strcmp(name, "proxy.config.net.transaction_no_activity_timeout_in") == 0) {
      Debug("net_queue", "proxy.config.net.transaction_no_activity_timeout_in updated to %" PRId64, data.rec_int);
      nh->transaction_no_activity_timeout_in = data.rec_int;
    }
    if (strcmp(name, "proxy.config.net.keep_alive_no_activity_timeout_in") == 0) {
      Debug("net_queue", "proxy.config.net.keep_alive_no_activity_timeout_in updated to %" PRId64, data.rec_int);
      nh->keep_alive_no_activity_timeout_in = data.rec_int;
    }
    if (strcmp(name, "proxy.config.net.default_inactivity_timeout") == 0) {
      Debug("net_queue", "proxy.config.net.default_inactivity_timeout updated to %" PRId64, data.rec_int);
      nh->default_inactivity_timeout = data.rec_int;
    }
  }

  if (update_per_thread_configuration == true) {
    nh->configure_per_thread();
  }

  return REC_ERR_OKAY;
}

//
// Initialization here, in the thread in which we will be executing
// from now on.
//
int
NetHandler::startNetEvent(int event, Event *e)
{
  // read configuration values and setup callbacks for when they change
  REC_ReadConfigInt32(max_connections_in, "proxy.config.net.max_connections_in");
  REC_ReadConfigInt32(max_connections_active_in, "proxy.config.net.max_connections_active_in");
  REC_ReadConfigInt32(inactive_threashold_in, "proxy.config.net.inactive_threashold_in");
  REC_ReadConfigInt32(transaction_no_activity_timeout_in, "proxy.config.net.transaction_no_activity_timeout_in");
  REC_ReadConfigInt32(keep_alive_no_activity_timeout_in, "proxy.config.net.keep_alive_no_activity_timeout_in");
  REC_ReadConfigInt32(default_inactivity_timeout, "proxy.config.net.default_inactivity_timeout");

  RecRegisterConfigUpdateCb("proxy.config.net.max_connections_in", update_nethandler_config, (void *)this);
  RecRegisterConfigUpdateCb("proxy.config.net.max_active_connections_in", update_nethandler_config, (void *)this);
  RecRegisterConfigUpdateCb("proxy.config.net.inactive_threashold_in", update_nethandler_config, (void *)this);
  RecRegisterConfigUpdateCb("proxy.config.net.transaction_no_activity_timeout_in", update_nethandler_config, (void *)this);
  RecRegisterConfigUpdateCb("proxy.config.net.keep_alive_no_activity_timeout_in", update_nethandler_config, (void *)this);
  RecRegisterConfigUpdateCb("proxy.config.net.default_inactivity_timeout", update_nethandler_config, (void *)this);

  Debug("net_queue", "proxy.config.net.max_connections_in updated to %d", max_connections_in);
  Debug("net_queue", "proxy.config.net.max_active_connections_in updated to %d", max_connections_active_in);
  Debug("net_queue", "proxy.config.net.inactive_threashold_in updated to %d", inactive_threashold_in);
  Debug("net_queue", "proxy.config.net.transaction_no_activity_timeout_in updated to %d", transaction_no_activity_timeout_in);
  Debug("net_queue", "proxy.config.net.keep_alive_no_activity_timeout_in updated to %d", keep_alive_no_activity_timeout_in);
  Debug("net_queue", "proxy.config.net.default_inactivity_timeout updated to %d", default_inactivity_timeout);

  configure_per_thread();

  (void)event;
  SET_HANDLER((NetContHandler)&NetHandler::mainNetEvent);
  e->schedule_every(-HRTIME_MSECONDS(net_event_period));
  trigger_event = e;
  return EVENT_CONT;
}

//
// Move VC's enabled on a different thread to the ready list
//
void
NetHandler::process_enabled_list(NetHandler *nh)
{
  UnixNetVConnection *vc = nullptr;

  SListM(UnixNetVConnection, NetState, read, enable_link) rq(nh->read_enable_list.popall());
  while ((vc = rq.pop())) {
    vc->ep.modify(EVENTIO_READ);
    vc->ep.refresh(EVENTIO_READ);
    vc->read.in_enabled_list = 0;
    if ((vc->read.enabled && vc->read.triggered) || vc->closed) {
      nh->read_ready_list.in_or_enqueue(vc);
    }
  }

  SListM(UnixNetVConnection, NetState, write, enable_link) wq(nh->write_enable_list.popall());
  while ((vc = wq.pop())) {
    vc->ep.modify(EVENTIO_WRITE);
    vc->ep.refresh(EVENTIO_WRITE);
    vc->write.in_enabled_list = 0;
    if ((vc->write.enabled && vc->write.triggered) || vc->closed) {
      nh->write_ready_list.in_or_enqueue(vc);
    }
  }
}

//
// The main event for NetHandler
// This is called every proxy.config.net.event_period, and handles all IO operations scheduled
// for this period.
//
int
NetHandler::mainNetEvent(int event, Event *e)
{
  ink_assert(trigger_event == e && (event == EVENT_INTERVAL || event == EVENT_POLL));
  (void)event;
  (void)e;
  EventIO *epd = nullptr;
  int poll_timeout;

  NET_INCREMENT_DYN_STAT(net_handler_run_stat);

  process_enabled_list(this);
  if (likely(!read_ready_list.empty() || !write_ready_list.empty() || !read_enable_list.empty() || !write_enable_list.empty())) {
    poll_timeout = 0; // poll immediately returns -- we have triggered stuff to process right now
  } else {
    poll_timeout = net_config_poll_timeout;
  }

  PollDescriptor *pd     = get_PollDescriptor(trigger_event->ethread);
  UnixNetVConnection *vc = nullptr;
#if TS_USE_EPOLL
  pd->result = epoll_wait(pd->epoll_fd, pd->ePoll_Triggered_Events, POLL_DESCRIPTOR_SIZE, poll_timeout);
  NetDebug("iocore_net_main_poll", "[NetHandler::mainNetEvent] epoll_wait(%d,%d), result=%d", pd->epoll_fd, poll_timeout,
           pd->result);
#elif TS_USE_KQUEUE
  struct timespec tv;
  tv.tv_sec  = poll_timeout / 1000;
  tv.tv_nsec = 1000000 * (poll_timeout % 1000);
  pd->result = kevent(pd->kqueue_fd, nullptr, 0, pd->kq_Triggered_Events, POLL_DESCRIPTOR_SIZE, &tv);
  NetDebug("iocore_net_main_poll", "[NetHandler::mainNetEvent] kevent(%d,%d), result=%d", pd->kqueue_fd, poll_timeout, pd->result);
#elif TS_USE_PORT
  int retval;
  timespec_t ptimeout;
  ptimeout.tv_sec  = poll_timeout / 1000;
  ptimeout.tv_nsec = 1000000 * (poll_timeout % 1000);
  unsigned nget    = 1;
  if ((retval = port_getn(pd->port_fd, pd->Port_Triggered_Events, POLL_DESCRIPTOR_SIZE, &nget, &ptimeout)) < 0) {
    pd->result = 0;
    switch (errno) {
    case EINTR:
    case EAGAIN:
    case ETIME:
      if (nget > 0) {
        pd->result = (int)nget;
      }
      break;
    default:
      ink_assert(!"unhandled port_getn() case:");
      break;
    }
  } else {
    pd->result = (int)nget;
  }
  NetDebug("iocore_net_main_poll", "[NetHandler::mainNetEvent] %d[%s]=port_getn(%d,%p,%d,%d,%d),results(%d)", retval,
           retval < 0 ? strerror(errno) : "ok", pd->port_fd, pd->Port_Triggered_Events, POLL_DESCRIPTOR_SIZE, nget, poll_timeout,
           pd->result);

#else
#error port me
#endif

  vc = nullptr;
  for (int x = 0; x < pd->result; x++) {
    epd = (EventIO *)get_ev_data(pd, x);
    if (epd->type == EVENTIO_READWRITE_VC) {
      vc = epd->data.vc;
      // Remove triggered NetVC from cop_list because it won't be timeout before next InactivityCop runs.
      if (cop_list.in(vc)) {
        cop_list.remove(vc);
      }
      if (get_ev_events(pd, x) & (EVENTIO_READ | EVENTIO_ERROR)) {
        vc->read.triggered = 1;
        if (!read_ready_list.in(vc)) {
          read_ready_list.enqueue(vc);
        } else if (get_ev_events(pd, x) & EVENTIO_ERROR) {
          // check for unhandled epoll events that should be handled
          Debug("iocore_net_main", "Unhandled epoll event on read: 0x%04x read.enabled=%d closed=%d read.netready_queue=%d",
                get_ev_events(pd, x), vc->read.enabled, vc->closed, read_ready_list.in(vc));
        }
      }
      vc = epd->data.vc;
      if (get_ev_events(pd, x) & (EVENTIO_WRITE | EVENTIO_ERROR)) {
        vc->write.triggered = 1;
        if (!write_ready_list.in(vc)) {
          write_ready_list.enqueue(vc);
        } else if (get_ev_events(pd, x) & EVENTIO_ERROR) {
          // check for unhandled epoll events that should be handled
          Debug("iocore_net_main", "Unhandled epoll event on write: 0x%04x write.enabled=%d closed=%d write.netready_queue=%d",
                get_ev_events(pd, x), vc->write.enabled, vc->closed, write_ready_list.in(vc));
        }
      } else if (!(get_ev_events(pd, x) & EVENTIO_READ)) {
        Debug("iocore_net_main", "Unhandled epoll event: 0x%04x", get_ev_events(pd, x));
      }
    } else if (epd->type == EVENTIO_DNS_CONNECTION) {
      if (epd->data.dnscon != nullptr) {
        epd->data.dnscon->trigger(); // Make sure the DNSHandler for this con knows we triggered
#if defined(USE_EDGE_TRIGGER)
        epd->refresh(EVENTIO_READ);
#endif
      }
    } else if (epd->type == EVENTIO_ASYNC_SIGNAL) {
      net_signal_hook_callback(trigger_event->ethread);
    }
    ev_next_event(pd, x);
  }

  pd->result = 0;

#if defined(USE_EDGE_TRIGGER)
  // UnixNetVConnection *
  while ((vc = read_ready_list.dequeue())) {
    // Initialize the thread-local continuation flags
    set_cont_flags(vc->control_flags);
    if (vc->closed)
      close_UnixNetVConnection(vc, trigger_event->ethread);
    else if (vc->read.enabled && vc->read.triggered)
      vc->net_read_io(this, trigger_event->ethread);
    else if (!vc->read.enabled) {
      read_ready_list.remove(vc);
#if defined(solaris)
      if (vc->read.triggered && vc->write.enabled) {
        vc->ep.modify(-EVENTIO_READ);
        vc->ep.refresh(EVENTIO_WRITE);
        vc->writeReschedule(this);
      }
#endif
    }
  }
  while ((vc = write_ready_list.dequeue())) {
    set_cont_flags(vc->control_flags);
    if (vc->closed)
      close_UnixNetVConnection(vc, trigger_event->ethread);
    else if (vc->write.enabled && vc->write.triggered)
      write_to_net(this, vc, trigger_event->ethread);
    else if (!vc->write.enabled) {
      write_ready_list.remove(vc);
#if defined(solaris)
      if (vc->write.triggered && vc->read.enabled) {
        vc->ep.modify(-EVENTIO_WRITE);
        vc->ep.refresh(EVENTIO_READ);
        vc->readReschedule(this);
      }
#endif
    }
  }
#else  /* !USE_EDGE_TRIGGER */
  while ((vc = read_ready_list.dequeue())) {
    diags->set_override(vc->control.debug_override);
    if (vc->closed)
      close_UnixNetVConnection(vc, trigger_event->ethread);
    else if (vc->read.enabled && vc->read.triggered)
      vc->net_read_io(this, trigger_event->ethread);
    else if (!vc->read.enabled)
      vc->ep.modify(-EVENTIO_READ);
  }
  while ((vc = write_ready_list.dequeue())) {
    diags->set_override(vc->control.debug_override);
    if (vc->closed)
      close_UnixNetVConnection(vc, trigger_event->ethread);
    else if (vc->write.enabled && vc->write.triggered)
      write_to_net(this, vc, trigger_event->ethread);
    else if (!vc->write.enabled)
      vc->ep.modify(-EVENTIO_WRITE);
  }
#endif /* !USE_EDGE_TRIGGER */

  return EVENT_CONT;
}

bool
NetHandler::manage_active_queue(bool ignore_queue_size = false)
{
  const int total_connections_in = active_queue_size + keep_alive_queue_size;
  Debug("net_queue", "max_connections_per_thread_in: %d max_connections_active_per_thread_in: %d total_connections_in: %d "
                     "active_queue_size: %d keep_alive_queue_size: %d",
        max_connections_per_thread_in, max_connections_active_per_thread_in, total_connections_in, active_queue_size,
        keep_alive_queue_size);

  if (ignore_queue_size == false && max_connections_active_per_thread_in > active_queue_size) {
    return true;
  }

  ink_hrtime now = Thread::get_hrtime();

  // loop over the non-active connections and try to close them
  UnixNetVConnection *vc      = active_queue.head;
  UnixNetVConnection *vc_next = nullptr;
  int closed                  = 0;
  int handle_event            = 0;
  int total_idle_time         = 0;
  int total_idle_count        = 0;
  for (; vc != nullptr; vc = vc_next) {
    vc_next = vc->active_queue_link.next;
    if ((vc->inactivity_timeout_in && vc->next_inactivity_timeout_at <= now) ||
        (vc->active_timeout_in && vc->next_activity_timeout_at <= now)) {
      _close_vc(vc, now, handle_event, closed, total_idle_time, total_idle_count);
    }
    if (ignore_queue_size == false && max_connections_active_per_thread_in > active_queue_size) {
      return true;
    }
  }

  if (max_connections_active_per_thread_in > active_queue_size) {
    return true;
  }

  return false; // failed to make room in the queue, all connections are active
}

void
NetHandler::configure_per_thread()
{
  // figure out the number of threads and calculate the number of connections per thread
  int threads                          = eventProcessor.thread_group[ET_NET]._count;
  max_connections_per_thread_in        = max_connections_in / threads;
  max_connections_active_per_thread_in = max_connections_active_in / threads;
  Debug("net_queue", "max_connections_per_thread_in updated to %d threads: %d", max_connections_per_thread_in, threads);
  Debug("net_queue", "max_connections_active_per_thread_in updated to %d threads: %d", max_connections_active_per_thread_in,
        threads);
}

void
NetHandler::manage_keep_alive_queue()
{
  uint32_t total_connections_in = active_queue_size + keep_alive_queue_size;
  ink_hrtime now                = Thread::get_hrtime();

  Debug("net_queue", "max_connections_per_thread_in: %d total_connections_in: %d active_queue_size: %d keep_alive_queue_size: %d",
        max_connections_per_thread_in, total_connections_in, active_queue_size, keep_alive_queue_size);

  if (!max_connections_per_thread_in || total_connections_in <= max_connections_per_thread_in) {
    return;
  }

  // loop over the non-active connections and try to close them
  UnixNetVConnection *vc_next = nullptr;
  int closed                  = 0;
  int handle_event            = 0;
  int total_idle_time         = 0;
  int total_idle_count        = 0;
  for (UnixNetVConnection *vc = keep_alive_queue.head; vc != nullptr; vc = vc_next) {
    vc_next = vc->keep_alive_queue_link.next;
    _close_vc(vc, now, handle_event, closed, total_idle_time, total_idle_count);

    total_connections_in = active_queue_size + keep_alive_queue_size;
    if (total_connections_in <= max_connections_per_thread_in) {
      break;
    }
  }

  if (total_idle_count > 0) {
    Debug("net_queue", "max cons: %d active: %d idle: %d already closed: %d, close event: %d mean idle: %d",
          max_connections_per_thread_in, total_connections_in, keep_alive_queue_size, closed, handle_event,
          total_idle_time / total_idle_count);
  }
}

void
NetHandler::_close_vc(UnixNetVConnection *vc, ink_hrtime now, int &handle_event, int &closed, int &total_idle_time,
                      int &total_idle_count)
{
  if (vc->thread != this_ethread()) {
    return;
  }
  MUTEX_TRY_LOCK(lock, vc->mutex, this_ethread());
  if (!lock.is_locked()) {
    return;
  }
  ink_hrtime diff = (now - (vc->next_inactivity_timeout_at - vc->inactivity_timeout_in)) / HRTIME_SECOND;
  if (diff > 0) {
    total_idle_time += diff;
    ++total_idle_count;
    NET_SUM_DYN_STAT(keep_alive_queue_timeout_total_stat, diff);
    NET_INCREMENT_DYN_STAT(keep_alive_queue_timeout_count_stat);
  }
  Debug("net_queue", "closing connection NetVC=%p idle: %u now: %" PRId64 " at: %" PRId64 " in: %" PRId64 " diff: %" PRId64, vc,
        keep_alive_queue_size, ink_hrtime_to_sec(now), ink_hrtime_to_sec(vc->next_inactivity_timeout_at),
        ink_hrtime_to_sec(vc->inactivity_timeout_in), diff);
  if (vc->closed) {
    close_UnixNetVConnection(vc, this_ethread());
    ++closed;
  } else {
    vc->next_inactivity_timeout_at = now;
    // create a dummy event
    Event event;
    event.ethread = this_ethread();
    if (vc->handleEvent(EVENT_IMMEDIATE, &event) == EVENT_DONE) {
      ++handle_event;
    }
  }
}

void
NetHandler::add_to_keep_alive_queue(UnixNetVConnection *vc)
{
  Debug("net_queue", "NetVC: %p", vc);

  if (keep_alive_queue.in(vc)) {
    // already in the keep-alive queue, move the head
    keep_alive_queue.remove(vc);
  } else {
    // in the active queue or no queue, new to this queue
    remove_from_active_queue(vc);
    ++keep_alive_queue_size;
  }
  keep_alive_queue.enqueue(vc);

  // if keep-alive queue is over size then close connections
  manage_keep_alive_queue();
}

void
NetHandler::remove_from_keep_alive_queue(UnixNetVConnection *vc)
{
  Debug("net_queue", "NetVC: %p", vc);
  if (keep_alive_queue.in(vc)) {
    keep_alive_queue.remove(vc);
    --keep_alive_queue_size;
  }
}

bool
NetHandler::add_to_active_queue(UnixNetVConnection *vc)
{
  Debug("net_queue", "NetVC: %p", vc);
  Debug("net_queue", "max_connections_per_thread_in: %d active_queue_size: %d keep_alive_queue_size: %d",
        max_connections_per_thread_in, active_queue_size, keep_alive_queue_size);

  // if active queue is over size then close inactive connections
  if (manage_active_queue() == false) {
    // there is no room left in the queue
    return false;
  }

  if (active_queue.in(vc)) {
    // already in the active queue, move the head
    active_queue.remove(vc);
  } else {
    // in the keep-alive queue or no queue, new to this queue
    remove_from_keep_alive_queue(vc);
    ++active_queue_size;
  }
  active_queue.enqueue(vc);

  return true;
}

void
NetHandler::remove_from_active_queue(UnixNetVConnection *vc)
{
  Debug("net_queue", "NetVC: %p", vc);
  if (active_queue.in(vc)) {
    active_queue.remove(vc);
    --active_queue_size;
  }
}
