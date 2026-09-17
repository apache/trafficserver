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

#include "P_UnixNetProcessor.h"
#include "P_Net.h"
#include "P_UnixNet.h"
#include "InactivityCop.h"
#include "iocore/net/AsyncSignalEventIO.h"
#include "tscore/ink_hrtime.h"
#include "ts/ats_probe.h"

#if TS_USE_LINUX_IO_URING
#include "iocore/io_uring/IO_URING.h"
#endif

#include <limits>

ink_hrtime        last_throttle_warning;
ink_hrtime        last_shedding_warning;
int               net_connections_throttle;
std::atomic<bool> net_memory_throttle = false;
int               fds_throttle;
ink_hrtime        last_transient_accept_error;

NetHandler::Config                                     NetHandler::global_config;
std::bitset<std::numeric_limits<unsigned int>::digits> NetHandler::active_thread_types;
/// The values @c NetHandler::configure_per_thread_values reads.
const std::bitset<NetHandler::CONFIG_ITEM_COUNT> NetHandler::config_value_affects_per_thread_value{
  (1ULL << static_cast<unsigned>(NetHandler::Config::Index::MAX_CONNECTIONS_IN)) |
  (1ULL << static_cast<unsigned>(NetHandler::Config::Index::MAX_REQUESTS_IN))};

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

void
initialize_thread_for_net(EThread *thread)
{
  NetHandler *nh = get_NetHandler(thread);

  new (reinterpret_cast<ink_dummy_for_new *>(nh)) NetHandler();
  new (reinterpret_cast<ink_dummy_for_new *>(get_PollCont(thread))) PollCont(thread->mutex, nh);
  nh->mutex  = new_ProxyMutex();
  nh->thread = thread;

  PollCont       *pc = get_PollCont(thread);
  PollDescriptor *pd = pc->pollDescriptor;

  InactivityCop *inactivityCop = new InactivityCop(nh->mutex, *nh);
  int            cop_freq      = 1;

  cop_freq = RecGetRecordInt("proxy.config.net.inactivity_check_frequency").value_or(0);
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
