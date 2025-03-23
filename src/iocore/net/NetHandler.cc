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
#include "P_UnixNet.h"
#include "iocore/net/NetHandler.h"
#include "iocore/net/PollCont.h"
#if TS_USE_LINUX_IO_URING
#include "iocore/io_uring/IO_URING.h"
#endif

#include <atomic>

using namespace std::literals;

namespace
{
DbgCtl dbg_ctl_net_queue{"net_queue"};
DbgCtl dbg_ctl_v_net_queue{"v_net_queue"};

} // end anonymous namespace

std::atomic<int32_t>  NetHandler::additional_accepts{0};
std::atomic<uint32_t> NetHandler::per_client_max_connections_in{0};

// NetHandler method definitions

NetHandler::NetHandler() : Continuation(nullptr)
{
  SET_HANDLER(&NetHandler::mainNetEvent);
}

int
NetHandler::startIO(NetEvent *ne)
{
  ink_assert(this->mutex->thread_holding == this_ethread());
  ink_assert(ne->get_thread() == this_ethread());
  int res = 0;

  PollDescriptor *pd = get_PollDescriptor(this->thread);
  if (ne->ep.start(pd, ne, this, EVENTIO_READ | EVENTIO_WRITE) < 0) {
    res = errno;
    // EEXIST should be ok, though it should have been cleared before we got back here
    if (errno != EEXIST) {
      Dbg(dbg_ctl_iocore_net, "NetHandler::startIO : failed on EventIO::start, errno = [%d](%s)", errno, strerror(errno));
      return -res;
    }
  }

  if (ne->read.triggered == 1) {
    read_ready_list.enqueue(ne);
  }
  ne->nh = this;
  return res;
}

void
NetHandler::stopIO(NetEvent *ne)
{
  ink_release_assert(ne->nh == this);

  ne->ep.stop();

  read_ready_list.remove(ne);
  write_ready_list.remove(ne);
  if (ne->read.in_enabled_list) {
    read_enable_list.remove(ne);
    ne->read.in_enabled_list = 0;
  }
  if (ne->write.in_enabled_list) {
    write_enable_list.remove(ne);
    ne->write.in_enabled_list = 0;
  }

  ne->nh = nullptr;
}

void
NetHandler::startCop(NetEvent *ne)
{
  ink_assert(this->mutex->thread_holding == this_ethread());
  ink_release_assert(ne->nh == this);
  ink_assert(!open_list.in(ne));

  open_list.enqueue(ne);
}

void
NetHandler::stopCop(NetEvent *ne)
{
  ink_release_assert(ne->nh == this);

  open_list.remove(ne);
  cop_list.remove(ne);
  remove_from_keep_alive_queue(ne);
  remove_from_active_queue(ne);
}

int
NetHandler::update_nethandler_config(const char *str, RecDataT, RecData data, void *)
{
  uint32_t        *updated_member = nullptr; // direct pointer to config member for update.
  std::string_view name{str};

  if (name == "proxy.config.net.max_connections_in"sv) {
    updated_member = &NetHandler::global_config.max_connections_in;
    Dbg(dbg_ctl_net_queue, "proxy.config.net.max_connections_in updated to %" PRId64, data.rec_int);
  } else if (name == "proxy.config.net.max_requests_in"sv) {
    updated_member = &NetHandler::global_config.max_requests_in;
    Dbg(dbg_ctl_net_queue, "proxy.config.net.max_requests_in updated to %" PRId64, data.rec_int);
  } else if (name == "proxy.config.net.inactive_threshold_in"sv) {
    updated_member = &NetHandler::global_config.inactive_threshold_in;
    Dbg(dbg_ctl_net_queue, "proxy.config.net.inactive_threshold_in updated to %" PRId64, data.rec_int);
  } else if (name == "proxy.config.net.transaction_no_activity_timeout_in"sv) {
    updated_member = &NetHandler::global_config.transaction_no_activity_timeout_in;
    Dbg(dbg_ctl_net_queue, "proxy.config.net.transaction_no_activity_timeout_in updated to %" PRId64, data.rec_int);
  } else if (name == "proxy.config.net.keep_alive_no_activity_timeout_in"sv) {
    updated_member = &NetHandler::global_config.keep_alive_no_activity_timeout_in;
    Dbg(dbg_ctl_net_queue, "proxy.config.net.keep_alive_no_activity_timeout_in updated to %" PRId64, data.rec_int);
  } else if (name == "proxy.config.net.default_inactivity_timeout"sv) {
    updated_member = &NetHandler::global_config.default_inactivity_timeout;
    Dbg(dbg_ctl_net_queue, "proxy.config.net.default_inactivity_timeout updated to %" PRId64, data.rec_int);
  } else if (name == "proxy.config.net.additional_accepts"sv) {
    NetHandler::additional_accepts.store(data.rec_int, std::memory_order_relaxed);
    Dbg(dbg_ctl_net_queue, "proxy.config.net.additional_accepts updated to %" PRId64, data.rec_int);
  } else if (name == "proxy.config.net.per_client.max_connections_in"sv) {
    NetHandler::per_client_max_connections_in.store(data.rec_int, std::memory_order_relaxed);
    Dbg(dbg_ctl_net_queue, "proxy.config.net.per_client.max_connections_in updated to %" PRId64, data.rec_int);
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
  global_config.max_connections_in                 = RecGetRecordInt("proxy.config.net.max_connections_in").first;
  global_config.max_requests_in                    = RecGetRecordInt("proxy.config.net.max_requests_in").first;
  global_config.inactive_threshold_in              = RecGetRecordInt("proxy.config.net.inactive_threshold_in").first;
  global_config.transaction_no_activity_timeout_in = RecGetRecordInt("proxy.config.net.transaction_no_activity_timeout_in").first;
  global_config.keep_alive_no_activity_timeout_in  = RecGetRecordInt("proxy.config.net.keep_alive_no_activity_timeout_in").first;
  global_config.default_inactivity_timeout         = RecGetRecordInt("proxy.config.net.default_inactivity_timeout").first;

  // Atomic configurations.
  {
    int32_t val = 0;
    val         = RecGetRecordInt("proxy.config.net.additional_accepts").first;
    additional_accepts.store(val, std::memory_order_relaxed);
  }
  {
    uint32_t val = 0;
    val          = RecGetRecordInt("proxy.config.net.per_client.max_connections_in").first;
    per_client_max_connections_in.store(val, std::memory_order_relaxed);
  }

  RecRegisterConfigUpdateCb("proxy.config.net.max_connections_in", update_nethandler_config, nullptr);
  RecRegisterConfigUpdateCb("proxy.config.net.max_requests_in", update_nethandler_config, nullptr);
  RecRegisterConfigUpdateCb("proxy.config.net.inactive_threshold_in", update_nethandler_config, nullptr);
  RecRegisterConfigUpdateCb("proxy.config.net.transaction_no_activity_timeout_in", update_nethandler_config, nullptr);
  RecRegisterConfigUpdateCb("proxy.config.net.keep_alive_no_activity_timeout_in", update_nethandler_config, nullptr);
  RecRegisterConfigUpdateCb("proxy.config.net.default_inactivity_timeout", update_nethandler_config, nullptr);
  RecRegisterConfigUpdateCb("proxy.config.net.additional_accepts", update_nethandler_config, nullptr);
  RecRegisterConfigUpdateCb("proxy.config.net.per_client.max_connections_in", update_nethandler_config, nullptr);

  Dbg(dbg_ctl_net_queue, "proxy.config.net.max_connections_in updated to %d", global_config.max_connections_in);
  Dbg(dbg_ctl_net_queue, "proxy.config.net.max_requests_in updated to %d", global_config.max_requests_in);
  Dbg(dbg_ctl_net_queue, "proxy.config.net.inactive_threshold_in updated to %d", global_config.inactive_threshold_in);
  Dbg(dbg_ctl_net_queue, "proxy.config.net.transaction_no_activity_timeout_in updated to %d",
      global_config.transaction_no_activity_timeout_in);
  Dbg(dbg_ctl_net_queue, "proxy.config.net.keep_alive_no_activity_timeout_in updated to %d",
      global_config.keep_alive_no_activity_timeout_in);
  Dbg(dbg_ctl_net_queue, "proxy.config.net.default_inactivity_timeout updated to %d", global_config.default_inactivity_timeout);
  Dbg(dbg_ctl_net_queue, "proxy.config.net.additional_accepts updated to %d", additional_accepts.load(std::memory_order_relaxed));
  Dbg(dbg_ctl_net_queue, "proxy.config.net.per_client.max_connections_in updated to %d",
      per_client_max_connections_in.load(std::memory_order_relaxed));
}

//
// Function used to release a NetEvent and free it.
//
void
NetHandler::free_netevent(NetEvent *ne)
{
  EThread *t = this->thread;

  ink_assert(t == this_ethread());
  ink_release_assert(ne->get_thread() == t);
  ink_release_assert(ne->nh == this);

  // Release ne from InactivityCop
  stopCop(ne);
  // Release ne from NetHandler
  stopIO(ne);
  // Clear and deallocate ne
  ne->free_thread(t);
}

//
// Move VC's enabled on a different thread to the ready list
//
void
NetHandler::process_enabled_list()
{
  NetEvent *ne = nullptr;

  SListM(NetEvent, NetState, read, enable_link) rq(read_enable_list.popall());
  while ((ne = rq.pop())) {
    ne->ep.modify(EVENTIO_READ);
    ne->ep.refresh(EVENTIO_READ);
    ne->read.in_enabled_list = 0;
    if ((ne->read.enabled && ne->read.triggered) || ne->closed) {
      read_ready_list.in_or_enqueue(ne);
    }
  }

  SListM(NetEvent, NetState, write, enable_link) wq(write_enable_list.popall());
  while ((ne = wq.pop())) {
    ne->ep.modify(EVENTIO_WRITE);
    ne->ep.refresh(EVENTIO_WRITE);
    ne->write.in_enabled_list = 0;
    if ((ne->write.enabled && ne->write.triggered) || ne->closed) {
      write_ready_list.in_or_enqueue(ne);
    }
  }
}

//
// Walk through the ready list
//
void
NetHandler::process_ready_list()
{
  NetEvent *ne = nullptr;

#if defined(USE_EDGE_TRIGGER)
  // NetEvent *
  while ((ne = read_ready_list.dequeue())) {
    // Initialize the thread-local continuation flags
    set_cont_flags(ne->get_control_flags());
    if (ne->closed) {
      free_netevent(ne);
    } else if (ne->read.enabled && ne->read.triggered) {
      ne->net_read_io(this);
    } else if (!ne->read.enabled) {
      read_ready_list.remove(ne);
    }
  }
  while ((ne = write_ready_list.dequeue())) {
    set_cont_flags(ne->get_control_flags());
    if (ne->closed) {
      free_netevent(ne);
    } else if (ne->write.enabled && ne->write.triggered) {
      ne->net_write_io(this);
    } else if (!ne->write.enabled) {
      write_ready_list.remove(ne);
    }
  }
#else  /* !USE_EDGE_TRIGGER */
  while ((ne = read_ready_list.dequeue())) {
    set_cont_flags(ne->get_control_flags());
    if (ne->closed)
      free_netevent(ne);
    else if (ne->read.enabled && ne->read.triggered)
      ne->net_read_io(this, this->thread);
    else if (!ne->read.enabled)
      ne->ep.modify(-EVENTIO_READ);
  }
  while ((ne = write_ready_list.dequeue())) {
    set_cont_flags(ne->get_control_flags());
    if (ne->closed)
      free_netevent(ne);
    else if (ne->write.enabled && ne->write.triggered)
      write_to_net(this, ne, this->thread);
    else if (!ne->write.enabled)
      ne->ep.modify(-EVENTIO_WRITE);
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
#if TS_USE_LINUX_IO_URING
  IOUringContext *ur = IOUringContext::local_context();
#endif

  Metrics::Counter::increment(net_rsb.handler_run);
  SCOPED_MUTEX_LOCK(lock, mutex, this->thread);

  process_enabled_list();

#if TS_USE_LINUX_IO_URING
  ur->submit();
#endif

  // Polling event by PollCont
  PollCont  *p        = get_PollCont(this->thread);
  ink_hrtime pre_poll = ink_get_hrtime();
  p->do_poll(timeout);
  ink_hrtime post_poll = ink_get_hrtime();
  ink_hrtime poll_time = post_poll - pre_poll;

  // Get & Process polling result
  PollDescriptor *pd = get_PollDescriptor(this->thread);
  for (int x = 0; x < pd->result; x++) {
    epd                                = static_cast<EventIO *> get_ev_data(pd, x);
    int                          flags = get_ev_events(pd, x);
    epd->process_event(flags);
    ev_next_event(pd, x);
  }

  pd->result = 0;

  process_ready_list();
  ink_hrtime post_process = ink_get_hrtime();
  ink_hrtime process_time = post_process - post_poll;
  this->thread->metrics.current_slice.load(std::memory_order_acquire)->record_io_stats(poll_time, process_time);

#if TS_USE_LINUX_IO_URING
  ur->service();
#endif

  return EVENT_CONT;
}

void
NetHandler::signalActivity()
{
#if HAVE_EVENTFD
  uint64_t counter = 1;
  ATS_UNUSED_RETURN(write(thread->evfd, &counter, sizeof(uint64_t)));
#else
  char dummy = 1;
  ATS_UNUSED_RETURN(write(thread->evpipe[1], &dummy, 1));
#endif
}

bool
NetHandler::manage_active_queue(NetEvent *enabling_ne, bool ignore_queue_size = false)
{
  const int total_connections_in = active_queue_size + keep_alive_queue_size;
  Dbg(dbg_ctl_v_net_queue,
      "max_connections_per_thread_in: %d max_requests_per_thread_in: %d total_connections_in: %d "
      "active_queue_size: %d keep_alive_queue_size: %d",
      max_connections_per_thread_in, max_requests_per_thread_in, total_connections_in, active_queue_size, keep_alive_queue_size);

  if (!max_requests_per_thread_in) {
    // active queue has no max
    return true;
  }

  if (ignore_queue_size == false && max_requests_per_thread_in > active_queue_size) {
    return true;
  }

  ink_hrtime now = ink_get_hrtime();

  // loop over the non-active connections and try to close them
  NetEvent *ne               = active_queue.head;
  NetEvent *ne_next          = nullptr;
  int       closed           = 0;
  int       handle_event     = 0;
  int       total_idle_time  = 0;
  int       total_idle_count = 0;
  for (; ne != nullptr; ne = ne_next) {
    ne_next = ne->active_queue_link.next;
    // It seems dangerous closing the current ne at this point
    // Let the activity_cop deal with it
    if (ne == enabling_ne) {
      continue;
    }
    if ((ne->next_inactivity_timeout_at && ne->next_inactivity_timeout_at <= now) ||
        (ne->next_activity_timeout_at && ne->next_activity_timeout_at <= now)) {
      _close_ne(ne, now, handle_event, closed, total_idle_time, total_idle_count);
    }
    if (ignore_queue_size == false && max_requests_per_thread_in > active_queue_size) {
      return true;
    }
  }

  if (max_requests_per_thread_in > active_queue_size) {
    return true;
  }

  return false; // failed to make room in the queue, all connections are active
}

void
NetHandler::configure_per_thread_values()
{
  // figure out the number of threads and calculate the number of connections per thread
  int threads                   = eventProcessor.thread_group[ET_NET]._count;
  max_connections_per_thread_in = config.max_connections_in / threads;
  max_requests_per_thread_in    = config.max_requests_in / threads;
  Dbg(dbg_ctl_net_queue, "max_connections_per_thread_in updated to %d threads: %d", max_connections_per_thread_in, threads);
  Dbg(dbg_ctl_net_queue, "max_requests_per_thread_in updated to %d threads: %d", max_requests_per_thread_in, threads);
}

void
NetHandler::manage_keep_alive_queue()
{
  uint32_t   total_connections_in = active_queue_size + keep_alive_queue_size;
  ink_hrtime now                  = ink_get_hrtime();

  Dbg(dbg_ctl_v_net_queue,
      "max_connections_per_thread_in: %d total_connections_in: %d active_queue_size: %d keep_alive_queue_size: %d",
      max_connections_per_thread_in, total_connections_in, active_queue_size, keep_alive_queue_size);

  if (!max_connections_per_thread_in || total_connections_in <= max_connections_per_thread_in) {
    return;
  }

  // loop over the non-active connections and try to close them
  NetEvent *ne_next          = nullptr;
  int       closed           = 0;
  int       handle_event     = 0;
  int       total_idle_time  = 0;
  int       total_idle_count = 0;
  for (NetEvent *ne = keep_alive_queue.head; ne != nullptr; ne = ne_next) {
    ne_next = ne->keep_alive_queue_link.next;
    _close_ne(ne, now, handle_event, closed, total_idle_time, total_idle_count);

    total_connections_in = active_queue_size + keep_alive_queue_size;
    if (total_connections_in <= max_connections_per_thread_in) {
      break;
    }
  }

  if (total_idle_count > 0) {
    Dbg(dbg_ctl_net_queue, "max cons: %d active: %d idle: %d already closed: %d, close event: %d mean idle: %d",
        max_connections_per_thread_in, total_connections_in, keep_alive_queue_size, closed, handle_event,
        total_idle_time / total_idle_count);
  }
}

void
NetHandler::_close_ne(NetEvent *ne, ink_hrtime now, int &handle_event, int &closed, int &total_idle_time, int &total_idle_count)
{
  if (ne->get_thread() != this_ethread()) {
    return;
  }
  MUTEX_TRY_LOCK(lock, ne->get_mutex(), this_ethread());
  if (!lock.is_locked()) {
    return;
  }
  ink_hrtime diff = (now - (ne->next_inactivity_timeout_at - ne->inactivity_timeout_in)) / HRTIME_SECOND;
  if (diff > 0) {
    total_idle_time += diff;
    ++total_idle_count;
    Metrics::Counter::increment(net_rsb.keep_alive_queue_timeout_total, diff);
    Metrics::Counter::increment(net_rsb.keep_alive_queue_timeout_count);
  }
  Dbg(dbg_ctl_net_queue, "closing connection NetEvent=%p idle: %u now: %" PRId64 " at: %" PRId64 " in: %" PRId64 " diff: %" PRId64,
      ne, keep_alive_queue_size, ink_hrtime_to_sec(now), ink_hrtime_to_sec(ne->next_inactivity_timeout_at),
      ink_hrtime_to_sec(ne->inactivity_timeout_in), diff);
  if (ne->closed) {
    free_netevent(ne);
    ++closed;
  } else {
    ne->next_inactivity_timeout_at = now;
    // create a dummy event
    Event event;
    event.ethread = this_ethread();
    if (ne->inactivity_timeout_in && ne->next_inactivity_timeout_at <= now) {
      if (ne->callback(VC_EVENT_INACTIVITY_TIMEOUT, &event) == EVENT_DONE) {
        ++handle_event;
      }
    } else if (ne->active_timeout_in && ne->next_activity_timeout_at <= now) {
      if (ne->callback(VC_EVENT_ACTIVE_TIMEOUT, &event) == EVENT_DONE) {
        ++handle_event;
      }
    }
  }
}

void
NetHandler::add_to_keep_alive_queue(NetEvent *ne)
{
  Dbg(dbg_ctl_net_queue, "NetEvent: %p", ne);
  ink_assert(mutex->thread_holding == this_ethread());

  if (keep_alive_queue.in(ne)) {
    // already in the keep-alive queue, move the head
    keep_alive_queue.remove(ne);
  } else {
    // in the active queue or no queue, new to this queue
    remove_from_active_queue(ne);
    ++keep_alive_queue_size;
  }
  keep_alive_queue.enqueue(ne);

  // if keep-alive queue is over size then close connections
  manage_keep_alive_queue();
}

void
NetHandler::remove_from_keep_alive_queue(NetEvent *ne)
{
  Dbg(dbg_ctl_net_queue, "NetEvent: %p", ne);
  ink_assert(mutex->thread_holding == this_ethread());

  if (keep_alive_queue.in(ne)) {
    keep_alive_queue.remove(ne);
    --keep_alive_queue_size;
  }
}

bool
NetHandler::add_to_active_queue(NetEvent *ne)
{
  Dbg(dbg_ctl_net_queue, "NetEvent: %p", ne);
  Dbg(dbg_ctl_net_queue, "max_connections_per_thread_in: %d active_queue_size: %d keep_alive_queue_size: %d",
      max_connections_per_thread_in, active_queue_size, keep_alive_queue_size);
  ink_assert(mutex->thread_holding == this_ethread());

  bool active_queue_full = false;

  // if active queue is over size then close inactive connections
  if (manage_active_queue(ne) == false) {
    active_queue_full = true;
  }

  if (active_queue.in(ne)) {
    // already in the active queue, move the head
    active_queue.remove(ne);
  } else {
    if (active_queue_full) {
      // there is no room left in the queue
      Metrics::Counter::increment(net_rsb.requests_max_throttled_in);
      return false;
    }
    // in the keep-alive queue or no queue, new to this queue
    remove_from_keep_alive_queue(ne);
    ++active_queue_size;
  }
  active_queue.enqueue(ne);

  return true;
}

void
NetHandler::remove_from_active_queue(NetEvent *ne)
{
  Dbg(dbg_ctl_net_queue, "NetEvent: %p", ne);
  ink_assert(mutex->thread_holding == this_ethread());

  if (active_queue.in(ne)) {
    active_queue.remove(ne);
    --active_queue_size;
  }
}

int
NetHandler::get_additional_accepts()
{
  int config_value = additional_accepts.load(std::memory_order_relaxed) + 1;
  return (config_value > 0 ? config_value : INT32_MAX - 1);
}

int
NetHandler::get_per_client_max_connections_in()
{
  int config_value = per_client_max_connections_in.load(std::memory_order_relaxed);
  return (config_value > 0 ? config_value : INT32_MAX - 1);
}
