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
int fds_throttle;
int fds_limit = 8000;
ink_hrtime last_transient_accept_error;
int n_netq_list = 32;

NetState::NetState():
enabled(0), priority(INK_MIN_PRIORITY), vio(VIO::NONE), queue(0), netready_queue(0),    //added by YTS Team, yamsat
  enable_queue(0),              //added by YTS Team, yamsat
  ifd(-1), do_next_at(0), next_vc(0), npending_scheds(0), triggered(0)  //added by YTS Team, yamsat
{
}

PollCont::PollCont(ProxyMutex * m):Continuation(m), net_handler(NULL), poll_timeout(REAL_DEFAULT_EPOLL_TIMEOUT)
{
  pollDescriptor = NEW(new PollDescriptor);
  pollDescriptor->init();
  pollDescriptor->seq_num = 0;
  SET_HANDLER(&PollCont::pollEvent);
}

PollCont::PollCont(ProxyMutex * m, NetHandler * nh):Continuation(m), net_handler(nh),
poll_timeout(REAL_DEFAULT_EPOLL_TIMEOUT)
{
  pollDescriptor = NEW(new PollDescriptor);
  pollDescriptor->init();
  pollDescriptor->seq_num = 0;
  SET_HANDLER(&PollCont::pollEvent);
}

PollCont::~PollCont()
{
  delete pollDescriptor;
}

//
// Changed by YTS Team, yamsat
// PollCont continuation which does the epoll_wait
// and stores the resultant events in ePoll_Triggered_Events
//
int
PollCont::pollEvent(int event, Event * e)
{
  (void) event;
  (void) e;

  if (likely(net_handler)) {
    /* checking to see whether there are connections on the ready_queue (either read or write) that need processing [ebalsa] */
    if (likely
        (!net_handler->ready_queue.read_ready_queue.empty() || !net_handler->ready_queue.write_ready_queue.empty() ||
         !net_handler->read_enable_list.empty() || !net_handler->write_enable_list.empty())) {
      Debug("epoll", "rrq: %d, wrq: %d, rel: %d, wel: %d", net_handler->ready_queue.read_ready_queue.empty(),
            net_handler->ready_queue.write_ready_queue.empty(), net_handler->read_enable_list.empty(),
            net_handler->write_enable_list.empty());
      poll_timeout = 0;         //poll immediately returns -- we have triggered stuff to process right now
    } else {
      poll_timeout = REAL_DEFAULT_EPOLL_TIMEOUT;
    }
  }
  // wait for fd's to tigger, or don't wait if timeout is 0
#if defined(USE_EPOLL)
  pollDescriptor->result = epoll_wait(pollDescriptor->epoll_fd,
                                      pollDescriptor->ePoll_Triggered_Events, POLL_DESCRIPTOR_SIZE, poll_timeout);
  Debug("epoll", "[PollCont::pollEvent] epoll_fd: %d, timeout: %d, results: %d", pollDescriptor->epoll_fd, poll_timeout,
        pollDescriptor->result);
  Debug("iocore_net", "pollEvent : Epoll fd %d active\n", pollDescriptor->epoll_fd);
#elif defined(USE_KQUEUE)
  struct timespec tv;
  tv.tv_sec = poll_timeout / 1000;
  tv.tv_nsec = 1000000 * (poll_timeout % 1000);
  pollDescriptor->result = kevent(pollDescriptor->kqueue_fd, NULL, 0,
                                  pollDescriptor->kq_Triggered_Events,
                                  POLL_DESCRIPTOR_SIZE,
                                  &tv);
  Debug("kqueue", "[PollCont::pollEvent] kueue_fd: %d, timeout: %d, results: %d", pollDescriptor->kqueue_fd, poll_timeout,
        pollDescriptor->result);
  Debug("iocore_net", "pollEvent : KQueue fd %d active\n", pollDescriptor->kqueue_fd);
#else
#error port me
#endif
  return EVENT_CONT;
}

void
initialize_thread_for_net(EThread * thread, int thread_index)
{
  static bool poll_delay_read = false;
  int max_poll_delay;           // max poll delay in milliseconds
  if (!poll_delay_read) {
    IOCORE_ReadConfigInteger(max_poll_delay, "proxy.config.net.max_poll_delay");
    if ((max_poll_delay & (max_poll_delay - 1)) ||
        (max_poll_delay<NET_PRIORITY_MSEC) || (max_poll_delay> MAX_NET_BUCKETS * NET_PRIORITY_MSEC)) {
      IOCORE_SignalWarning(REC_SIGNAL_SYSTEM_ERROR, "proxy.config.net.max_poll_delay range is [4-1024]");
    } else
      n_netq_list = max_poll_delay / 4;
    poll_delay_read = true;
  }

  new((ink_dummy_for_new *) get_NetHandler(thread)) NetHandler(false);
  new((ink_dummy_for_new *) get_PollCont(thread)) PollCont(thread->mutex, get_NetHandler(thread));
  get_NetHandler(thread)->mutex = new_ProxyMutex();
  get_NetHandler(thread)->read_enable_mutex = new_ProxyMutex();
  get_NetHandler(thread)->write_enable_mutex = new_ProxyMutex();
  thread->schedule_imm(get_NetHandler(thread));

#ifndef INACTIVITY_TIMEOUT
  InactivityCop *inactivityCop = NEW(new InactivityCop(get_NetHandler(thread)->mutex));
  thread->schedule_every(inactivityCop, HRTIME_SECONDS(1));
#endif
}

// NetHandler method definitions

NetHandler::NetHandler(bool _ext_main):Continuation(NULL), trigger_event(0), cur_msec(0), ext_main(_ext_main)
{
  SET_HANDLER((NetContHandler) & NetHandler::startNetEvent);
}

//
// Initialization here, in the thread in which we will be executing
// from now on.
//
int
NetHandler::startNetEvent(int event, Event * e)
{
  (void) event;
  SET_HANDLER((NetContHandler) & NetHandler::mainNetEvent);
  e->schedule_every(NET_PERIOD);
  trigger_event = e;
  return EVENT_CONT;
}

//
//Function added by YTS Team, yamsat
//
void
NetHandler::process_sm_enabled_list(NetHandler * nh, EThread * t)
{

  UnixNetVConnection *vc = NULL;

  MUTEX_TRY_LOCK(rlistlock, nh->read_enable_mutex, t);
  if (rlistlock) {
    Queue<UnixNetVConnection> &rq = nh->read_enable_list;
    while ((vc = (UnixNetVConnection *) rq.dequeue(rq.head, rq.head->read.enable_link))) {
      vc->read.enable_queue = NULL;
      if ((vc->read.enabled && vc->read.triggered) || vc->closed) {
        if (!vc->read.netready_queue) {
          nh->ready_queue.epoll_addto_read_ready_queue(vc);
        }
      }
    }
    MUTEX_RELEASE(rlistlock);
  }

  vc = NULL;

  MUTEX_TRY_LOCK(wlistlock, nh->write_enable_mutex, t);
  if (wlistlock) {
    Queue<UnixNetVConnection> &wq = nh->write_enable_list;
    while ((vc = (UnixNetVConnection *) wq.dequeue(wq.head, wq.head->write.enable_link))) {
      vc->write.enable_queue = NULL;
      if ((vc->write.enabled && vc->write.triggered) || vc->closed) {
        if (!vc->write.netready_queue) {
          nh->ready_queue.epoll_addto_write_ready_queue(vc);
        }
      }
    }
    MUTEX_RELEASE(wlistlock);
  }
}

//
// The main event for NetHandler
// This is called every NET_PERIOD, and handles all IO operations scheduled
// for this period.
//
int
NetHandler::mainNetEvent(int event, Event * e)
{
  ink_assert(trigger_event == e && (event == EVENT_INTERVAL || event == EVENT_POLL));
  (void) event;
  (void) e;
  struct epoll_data_ptr *epd = NULL;
  int poll_timeout = REAL_DEFAULT_EPOLL_TIMEOUT;

  NET_INCREMENT_DYN_STAT(net_handler_run_stat);
  //UnixNetVConnection *closed_vc = NULL, *next_closed_vc = NULL;
  //Queue<UnixNetVConnection> &q = wait_list.read_wait_list;
  //for (closed_vc= (UnixNetVConnection*)q.head ; closed_vc ; closed_vc = next_closed_vc){
  // next_closed_vc = (UnixNetVConnection*) closed_vc->read.link.next;
  //if (closed_vc->closed){
  //printf("MESSEDUP connection closed for fd :%d\n",closed_vc->con.fd);
  //close_UnixNetVConnection(closed_vc, trigger_event->ethread);
  //}
  //}

  process_sm_enabled_list(this, e->ethread);
  if (likely(!ready_queue.read_ready_queue.empty() || !ready_queue.write_ready_queue.empty() ||
             !read_enable_list.empty() || !write_enable_list.empty())) {
    poll_timeout = 0;           //poll immediately returns -- we have triggered stuff to process right now
  } else {
    poll_timeout = REAL_DEFAULT_EPOLL_TIMEOUT;
  }

  PollDescriptor *pd = get_PollDescriptor(trigger_event->ethread);
  UnixNetVConnection *vc = NULL;
#if defined(USE_EPOLL)
  pd->result = epoll_wait(pd->epoll_fd, pd->ePoll_Triggered_Events, POLL_DESCRIPTOR_SIZE, poll_timeout);
#elif defined(USE_KQUEUE)
  struct timespec tv;
  tv.tv_sec = poll_timeout / 1000;
  tv.tv_nsec = 1000000 * (poll_timeout % 1000);
  pd->result = kevent(pd->kqueue_fd, NULL, 0,
                      pd->kq_Triggered_Events, POLL_DESCRIPTOR_SIZE,
                      &tv);

#else
#error port me
#endif

  vc = NULL;

  for (int x = 0; x < pd->result; x++) {
    epd = (struct epoll_data_ptr *) get_ev_data(pd,x);
    if (epd->type == EPOLL_READWRITE_VC) {
      vc = epd->data.vc;
      if (get_ev_events(pd,x) & (INK_EVP_IN)) {
        vc->read.triggered = 1;
        vc->addLogMessage("read triggered");
        if ((vc->read.enabled || vc->closed) && !vc->read.netready_queue) {
          ready_queue.epoll_addto_read_ready_queue(vc);
        } else if (get_ev_events(pd,x) & (INK_EVP_PRI | INK_EVP_HUP | INK_EVP_ERR)) {
          // check for unhandled epoll events that should be handled
          Debug("epoll_miss", "Unhandled epoll event on read: 0x%04x read.enabled=%d closed=%d read.netready_queue=%p",
                get_ev_events(pd,x), vc->read.enabled, vc->closed, vc->read.netready_queue);
        }
      }
      vc = epd->data.vc;
      if (get_ev_events(pd,x) & (INK_EVP_OUT)) {
        vc->write.triggered = 1;
        vc->addLogMessage("write triggered");
        if ((vc->write.enabled || vc->closed) && !vc->write.netready_queue) {
          ready_queue.epoll_addto_write_ready_queue(vc);
        } else if (get_ev_events(pd,x) & (INK_EVP_PRI | INK_EVP_HUP | INK_EVP_ERR)) {
          // check for unhandled epoll events that should be handled
          Debug("epoll_miss",
                "Unhandled epoll event on write: 0x%04x write.enabled=%d closed=%d write.netready_queue=%p",
                get_ev_events(pd,x), vc->write.enabled, vc->closed, vc->write.netready_queue);
        }
      } else if (!(get_ev_events(pd,x) & (INK_EVP_IN)) &&
                 get_ev_events(pd,x) & (INK_EVP_PRI | INK_EVP_HUP | INK_EVP_ERR)) {
        Debug("epoll_miss", "Unhandled epoll event: 0x%04x", get_ev_events(pd,x));
      }
    } else if (epd->type == EPOLL_DNS_CONNECTION) {
      if (epd->data.dnscon != NULL) {
        dnsqueue.enqueue(epd->data.dnscon, epd->data.dnscon->link);
      }
    }
  }

  pd->result = 0;

  UnixNetVConnection *next_vc = NULL;
  vc = ready_queue.read_ready_queue.head;

  while (vc) {
    next_vc = vc->read.netready_link.next;
    if (vc->closed) {
      close_UnixNetVConnection(vc, trigger_event->ethread);
    } else if (vc->read.enabled && vc->read.triggered) {
      vc->net_read_io(this, trigger_event->ethread);
    }
    vc = next_vc;
  }

  next_vc = NULL;
  vc = ready_queue.write_ready_queue.head;

  while (vc) {
    next_vc = vc->write.netready_link.next;
    if (vc->closed) {
      close_UnixNetVConnection(vc, trigger_event->ethread);
    } else if (vc->write.enabled && vc->write.triggered) {
      write_to_net(this, vc, pd, trigger_event->ethread);
    }
    vc = next_vc;
  }

  return EVENT_CONT;
}

// PriorityPollQueue methods

PriorityPollQueue::PriorityPollQueue()
{
  position = ink_get_hrtime() / HRTIME_MSECOND;
}
