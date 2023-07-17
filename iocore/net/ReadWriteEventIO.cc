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

#include "ReadWriteEventIO.h"
#include "NetHandler.h"

int
ReadWriteEventIO::start(EventLoop l, NetEvent *ne, NetHandler *nh, int events)
{
  _ne = ne;
  _nh = nh;
  return start_common(l, ne->get_fd(), events);
}

int
ReadWriteEventIO::start(EventLoop l, int afd, NetEvent *ne, NetHandler *nh, int events)
{
  _ne = ne;
  _nh = nh;
  return start_common(l, afd, events);
}

void
ReadWriteEventIO::process_event(int flags)
{
  // Remove triggered NetEvent from cop_list because it won't be timeout before
  // next InactivityCop runs.
  if (_nh->cop_list.in(_ne)) {
    _nh->cop_list.remove(_ne);
  }
  if (flags & (EVENTIO_ERROR)) {
    _ne->set_error_from_socket();
  }
  if (flags & (EVENTIO_READ)) {
    _ne->read.triggered = 1;
    if (!_nh->read_ready_list.in(_ne)) {
      _nh->read_ready_list.enqueue(_ne);
    }
  }
  if (flags & (EVENTIO_WRITE)) {
    _ne->write.triggered = 1;
    if (!_nh->write_ready_list.in(_ne)) {
      _nh->write_ready_list.enqueue(_ne);
    }
  } else if (!(flags & (EVENTIO_READ))) {
    Debug("iocore_net_main", "Unhandled epoll event: 0x%04x", flags);
    // In practice we sometimes see EPOLLERR and EPOLLHUP through there
    // Anything else would be surprising
    ink_assert((flags & ~(EVENTIO_ERROR)) == 0);
    _ne->write.triggered = 1;
    if (!_nh->write_ready_list.in(_ne)) {
      _nh->write_ready_list.enqueue(_ne);
    }
  }
}
