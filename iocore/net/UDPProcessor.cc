/** @file

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

#include "UDPProcessor.h"
#include "P_Net.h"

UDP2NetProcessor udp2Net;
EventType ET_UDP2;

void
initialize_thread_for_udp2_net(EThread *thread)
{
  NetHandler *nh = get_NetHandler(thread);

  new (reinterpret_cast<ink_dummy_for_new *>(nh)) NetHandler();
  new (reinterpret_cast<ink_dummy_for_new *>(get_PollCont(thread))) PollCont(thread->mutex, nh);
  nh->mutex  = new_ProxyMutex();
  nh->thread = thread;

  PollCont *pc       = get_PollCont(thread);
  PollDescriptor *pd = pc->pollDescriptor;

  memcpy(&nh->config, &NetHandler::global_config, sizeof(NetHandler::global_config));
  nh->configure_per_thread_values();

  thread->set_tail_handler(nh);
  thread->ep = static_cast<EventIO *>(ats_malloc(sizeof(EventIO)));
  new (thread->ep) EventIO();
  thread->ep->type = EVENTIO_ASYNC_SIGNAL;
#if HAVE_EVENTFD
  thread->ep->start(pd, thread->evfd, nullptr, EVENTIO_READ);
#else
  thread->ep->start(pd, thread->evpipe[0], nullptr, EVENTIO_READ);
#endif
}

int
UDP2NetProcessor::start(int n_upd_threads, size_t stacksize)
{
  if (n_upd_threads < 1) {
    return -1;
  }

  if (unix_netProcessor.pollCont_offset < 0) {
    unix_netProcessor.pollCont_offset = eventProcessor.allocate(sizeof(PollCont));
  }

  if (unix_netProcessor.netHandler_offset < 0)
    unix_netProcessor.netHandler_offset = eventProcessor.allocate(sizeof(NetHandler));

  ET_UDP2 = eventProcessor.register_event_type("ET_UDP2");
  eventProcessor.schedule_spawn(&initialize_thread_for_udp2_net, ET_UDP2);
  eventProcessor.spawn_event_threads(ET_UDP2, n_upd_threads, stacksize);
  return 0;
}
