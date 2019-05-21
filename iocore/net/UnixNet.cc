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

using namespace std::literals;

ink_hrtime last_throttle_warning;
ink_hrtime last_shedding_warning;
int net_connections_throttle;
bool net_memory_throttle = false;
int fds_throttle;
int fds_limit = 8000;
ink_hrtime last_transient_accept_error;

NetHandler::Config NetHandler::global_config;
std::bitset<std::numeric_limits<unsigned int>::digits> NetHandler::active_thread_types;
const std::bitset<NetHandler::CONFIG_ITEM_COUNT> NetHandler::config_value_affects_per_thread_value{0x3};

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
        nh.free_netvc(vc);
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
PollCont::pollEvent(int, Event *)
{
  this->do_poll(-1);
  return EVENT_CONT;
}

void
PollCont::do_poll(ink_hrtime timeout)
{
  if (likely(net_handler)) {
    /* checking to see whether there are connections on the ready_queue (either read or write) that need processing [ebalsa] */
    if (likely(!net_handler->read_ready_list.empty() || !net_handler->write_ready_list.empty() ||
               !net_handler->read_enable_list.empty() || !net_handler->write_enable_list.empty())) {
      NetDebug("iocore_net_poll", "rrq: %d, wrq: %d, rel: %d, wel: %d", net_handler->read_ready_list.empty(),
               net_handler->write_ready_list.empty(), net_handler->read_enable_list.empty(),
               net_handler->write_enable_list.empty());
      poll_timeout = 0; // poll immediately returns -- we have triggered stuff to process right now
    } else if (timeout >= 0) {
      poll_timeout = ink_hrtime_to_msec(timeout);
    } else {
      poll_timeout = net_config_poll_timeout;
    }
  }
// wait for fd's to trigger, or don't wait if timeout is 0
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
  thread->ep       = static_cast<EventIO *>(ats_malloc(sizeof(EventIO)));
  thread->ep->type = EVENTIO_ASYNC_SIGNAL;
#if HAVE_EVENTFD
  thread->ep->start(pd, thread->evfd, nullptr, EVENTIO_READ);
#else
  thread->ep->start(pd, thread->evpipe[0], nullptr, EVENTIO_READ);
#endif
}

// NetHandler method definitions

NetHandler::NetHandler() : Continuation(nullptr)
{
  SET_HANDLER((NetContHandler)&NetHandler::mainNetEvent);
}

int
NetHandler::update_nethandler_config(const char *str, RecDataT, RecData data, void *)
{
  uint32_t *updated_member = nullptr; // direct pointer to config member for update.
  std::string_view name{str};

  if (name == "proxy.config.net.max_connections_in"sv) {
    updated_member = &NetHandler::global_config.max_connections_in;
    Debug("net_queue", "proxy.config.net.max_connections_in updated to %" PRId64, data.rec_int);
  } else if (name == "proxy.config.net.max_active_connections_in"sv) {
    updated_member = &NetHandler::global_config.max_connections_active_in;
    Debug("net_queue", "proxy.config.net.max_active_connections_in updated to %" PRId64, data.rec_int);
  } else if (name == "proxy.config.net.inactive_threshold_in"sv) {
    updated_member = &NetHandler::global_config.inactive_threshold_in;
    Debug("net_queue", "proxy.config.net.inactive_threshold_in updated to %" PRId64, data.rec_int);
  } else if (name == "proxy.config.net.transaction_no_activity_timeout_in"sv) {
    updated_member = &NetHandler::global_config.transaction_no_activity_timeout_in;
    Debug("net_queue", "proxy.config.net.transaction_no_activity_timeout_in updated to %" PRId64, data.rec_int);
  } else if (name == "proxy.config.net.keep_alive_no_activity_timeout_in"sv) {
    updated_member = &NetHandler::global_config.keep_alive_no_activity_timeout_in;
    Debug("net_queue", "proxy.config.net.keep_alive_no_activity_timeout_in updated to %" PRId64, data.rec_int);
  } else if (name == "proxy.config.net.default_inactivity_timeout"sv) {
    updated_member = &NetHandler::global_config.default_inactivity_timeout;
    Debug("net_queue", "proxy.config.net.default_inactivity_timeout updated to %" PRId64, data.rec_int);
  }

  if (updated_member) {
    *updated_member = data.rec_int; // do the actual update.
    // portable form of the update, an index converted to <void*> so it can be passed as an event cookie.
    void *idx = reinterpret_cast<void *>(static_cast<intptr_t>(updated_member - &global_config[0]));
    // Signal the NetHandler instances, passing the index of the updated config value.
    for (int i = 0; i < eventProcessor.n_thread_groups; ++i) {
      if (!active_thread_types[i]) {
        continue;
      }
      for (EThread **tp    = eventProcessor.thread_group[i]._thread,
                   **limit = eventProcessor.thread_group[i]._thread + eventProcessor.thread_group[i]._count;
           tp < limit; ++tp) {
        NetHandler *nh = get_NetHandler(*tp);
        if (nh) {
          nh->thread->schedule_imm(nh, TS_EVENT_MGMT_UPDATE, idx);
        }
      }
    }
  }

  return REC_ERR_OKAY;
}

void
NetHandler::init_for_process()
{
  // read configuration values and setup callbacks for when they change
  REC_ReadConfigInt32(global_config.max_connections_in, "proxy.config.net.max_connections_in");
  REC_ReadConfigInt32(global_config.max_connections_active_in, "proxy.config.net.max_connections_active_in");
  REC_ReadConfigInt32(global_config.inactive_threshold_in, "proxy.config.net.inactive_threshold_in");
  REC_ReadConfigInt32(global_config.transaction_no_activity_timeout_in, "proxy.config.net.transaction_no_activity_timeout_in");
  REC_ReadConfigInt32(global_config.keep_alive_no_activity_timeout_in, "proxy.config.net.keep_alive_no_activity_timeout_in");
  REC_ReadConfigInt32(global_config.default_inactivity_timeout, "proxy.config.net.default_inactivity_timeout");

  RecRegisterConfigUpdateCb("proxy.config.net.max_connections_in", update_nethandler_config, nullptr);
  RecRegisterConfigUpdateCb("proxy.config.net.max_active_connections_in", update_nethandler_config, nullptr);
  RecRegisterConfigUpdateCb("proxy.config.net.inactive_threshold_in", update_nethandler_config, nullptr);
  RecRegisterConfigUpdateCb("proxy.config.net.transaction_no_activity_timeout_in", update_nethandler_config, nullptr);
  RecRegisterConfigUpdateCb("proxy.config.net.keep_alive_no_activity_timeout_in", update_nethandler_config, nullptr);
  RecRegisterConfigUpdateCb("proxy.config.net.default_inactivity_timeout", update_nethandler_config, nullptr);

  Debug("net_queue", "proxy.config.net.max_connections_in updated to %d", global_config.max_connections_in);
  Debug("net_queue", "proxy.config.net.max_active_connections_in updated to %d", global_config.max_connections_active_in);
  Debug("net_queue", "proxy.config.net.inactive_threshold_in updated to %d", global_config.inactive_threshold_in);
  Debug("net_queue", "proxy.config.net.transaction_no_activity_timeout_in updated to %d",
        global_config.transaction_no_activity_timeout_in);
  Debug("net_queue", "proxy.config.net.keep_alive_no_activity_timeout_in updated to %d",
        global_config.keep_alive_no_activity_timeout_in);
  Debug("net_queue", "proxy.config.net.default_inactivity_timeout updated to %d", global_config.default_inactivity_timeout);
}

//
// Function used to release a UnixNetVConnection and free it.
//
void
NetHandler::free_netvc(UnixNetVConnection *netvc)
{
  EThread *t = this->thread;

  ink_assert(t == this_ethread());
  ink_release_assert(netvc->thread == t);
  ink_release_assert(netvc->nh == this);

  // Release netvc from InactivityCop
  stopCop(netvc);
  // Release netvc from NetHandler
  stopIO(netvc);
  // Clear and deallocate netvc
  netvc->free(t);
}

//
// Move VC's enabled on a different thread to the ready list
//
void
NetHandler::process_enabled_list()
{
  UnixNetVConnection *vc = nullptr;

  SListM(UnixNetVConnection, NetState, read, enable_link) rq(read_enable_list.popall());
  while ((vc = rq.pop())) {
    vc->ep.modify(EVENTIO_READ);
    vc->ep.refresh(EVENTIO_READ);
    vc->read.in_enabled_list = 0;
    if ((vc->read.enabled && vc->read.triggered) || vc->closed) {
      read_ready_list.in_or_enqueue(vc);
    }
  }

  SListM(UnixNetVConnection, NetState, write, enable_link) wq(write_enable_list.popall());
  while ((vc = wq.pop())) {
    vc->ep.modify(EVENTIO_WRITE);
    vc->ep.refresh(EVENTIO_WRITE);
    vc->write.in_enabled_list = 0;
    if ((vc->write.enabled && vc->write.triggered) || vc->closed) {
      write_ready_list.in_or_enqueue(vc);
    }
  }
}

//
// Walk through the ready list
//
void
NetHandler::process_ready_list()
{
  UnixNetVConnection *vc = nullptr;

#if defined(USE_EDGE_TRIGGER)
  // UnixNetVConnection *
  while ((vc = read_ready_list.dequeue())) {
    // Initialize the thread-local continuation flags
    set_cont_flags(vc->control_flags);
    if (vc->closed) {
      free_netvc(vc);
    } else if (vc->read.enabled && vc->read.triggered) {
      vc->net_read_io(this, this->thread);
    } else if (!vc->read.enabled) {
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
    if (vc->closed) {
      free_netvc(vc);
    } else if (vc->write.enabled && vc->write.triggered) {
      write_to_net(this, vc, this->thread);
    } else if (!vc->write.enabled) {
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
    set_cont_flags(vc->control_flags);
    if (vc->closed)
      free_netvc(vc);
    else if (vc->read.enabled && vc->read.triggered)
      vc->net_read_io(this, this->thread);
    else if (!vc->read.enabled)
      vc->ep.modify(-EVENTIO_READ);
  }
  while ((vc = write_ready_list.dequeue())) {
    set_cont_flags(vc->control_flags);
    if (vc->closed)
      free_netvc(vc);
    else if (vc->write.enabled && vc->write.triggered)
      write_to_net(this, vc, this->thread);
    else if (!vc->write.enabled)
      vc->ep.modify(-EVENTIO_WRITE);
  }
#endif /* !USE_EDGE_TRIGGER */
}

//
// The main event for NetHandler
int
NetHandler::mainNetEvent(int event, Event *e)
{
  if (TS_EVENT_MGMT_UPDATE == event) {
    intptr_t idx = reinterpret_cast<intptr_t>(e->cookie);
    // Copy to the same offset in the instance struct.
    config[idx] = global_config[idx];
    if (config_value_affects_per_thread_value[idx]) {
      this->configure_per_thread_values();
    }
    return EVENT_CONT;
  } else {
    ink_assert(trigger_event == e && (event == EVENT_INTERVAL || event == EVENT_POLL));
    return this->waitForActivity(-1);
  }
}

int
NetHandler::waitForActivity(ink_hrtime timeout)
{
  EventIO *epd = nullptr;

  NET_INCREMENT_DYN_STAT(net_handler_run_stat);
  SCOPED_MUTEX_LOCK(lock, mutex, this->thread);

  process_enabled_list();

  // Polling event by PollCont
  PollCont *p = get_PollCont(this->thread);
  p->do_poll(timeout);

  // Get & Process polling result
  PollDescriptor *pd     = get_PollDescriptor(this->thread);
  UnixNetVConnection *vc = nullptr;
  for (int x = 0; x < pd->result; x++) {
    epd = static_cast<EventIO *> get_ev_data(pd, x);
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
      net_signal_hook_callback(this->thread);
    }
    ev_next_event(pd, x);
  }

  pd->result = 0;

  process_ready_list();

  return EVENT_CONT;
}

void
NetHandler::signalActivity()
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

bool
NetHandler::manage_active_queue(bool ignore_queue_size = false)
{
  const int total_connections_in = active_queue_size + keep_alive_queue_size;
  Debug("net_queue",
        "max_connections_per_thread_in: %d max_connections_active_per_thread_in: %d total_connections_in: %d "
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
NetHandler::configure_per_thread_values()
{
  // figure out the number of threads and calculate the number of connections per thread
  int threads                          = eventProcessor.thread_group[ET_NET]._count;
  max_connections_per_thread_in        = config.max_connections_in / threads;
  max_connections_active_per_thread_in = config.max_connections_active_in / threads;
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
    free_netvc(vc);
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
  ink_assert(mutex->thread_holding == this_ethread());

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
  ink_assert(mutex->thread_holding == this_ethread());

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
  ink_assert(mutex->thread_holding == this_ethread());

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
  ink_assert(mutex->thread_holding == this_ethread());

  if (active_queue.in(vc)) {
    active_queue.remove(vc);
    --active_queue_size;
  }
}
