/**@file

   A brief file description

 @section license License

   Licensed to the Apache Software
   Foundation(ASF) under one
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

#include "iocore/net/PollCont.h"
#include "P_Net.h"

namespace
{
DbgCtl dbg_ctl_iocore_net_poll{"iocore_net_poll"};
DbgCtl dbg_ctl_v_iocore_net_poll{"v_iocore_net_poll"};
} // end anonymous namespace

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
    /* checking to see whether there are connections on the ready_queue (either
     * read or write) that need processing [ebalsa] */
    if (likely(!net_handler->read_ready_list.empty() || !net_handler->write_ready_list.empty() ||
               !net_handler->read_enable_list.empty() || !net_handler->write_enable_list.empty())) {
      NetDbg(dbg_ctl_iocore_net_poll, "rrq: %d, wrq: %d, rel: %d, wel: %d", net_handler->read_ready_list.empty(),
             net_handler->write_ready_list.empty(), net_handler->read_enable_list.empty(), net_handler->write_enable_list.empty());
      poll_timeout = 0; // poll immediately returns -- we have triggered stuff
                        // to process right now
    } else if (timeout >= 0) {
      poll_timeout = ink_hrtime_to_msec(timeout);
    } else {
      poll_timeout = EThread::default_wait_interval_ms;
    }
  }
// wait for fd's to trigger, or don't wait if timeout is 0
#if TS_USE_EPOLL
  pollDescriptor->result =
    epoll_wait(pollDescriptor->epoll_fd, pollDescriptor->ePoll_Triggered_Events, POLL_DESCRIPTOR_SIZE, poll_timeout);
  NetDbg(dbg_ctl_v_iocore_net_poll, "[PollCont::pollEvent] epoll_fd: %d, timeout: %d, results: %d", pollDescriptor->epoll_fd,
         poll_timeout, pollDescriptor->result);
#elif TS_USE_KQUEUE
  struct timespec tv;
  tv.tv_sec  = poll_timeout / 1000;
  tv.tv_nsec = 1000000 * (poll_timeout % 1000);
  pollDescriptor->result =
    kevent(pollDescriptor->kqueue_fd, nullptr, 0, pollDescriptor->kq_Triggered_Events, POLL_DESCRIPTOR_SIZE, &tv);
  NetDbg(dbg_ctl_v_iocore_net_poll, "[PollCont::pollEvent] kqueue_fd: %d, timeout: %d, results: %d", pollDescriptor->kqueue_fd,
         poll_timeout, pollDescriptor->result);
#endif
}
