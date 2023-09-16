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

#include "UDPEventIO.h"
#include "P_UDPNet.h"

int
UDPEventIO::start(EventLoop l, UnixUDPConnection *uc, UDPNetHandler *uh, int events)
{
  _uc = uc;
  _uh = uh;
  return start_common(l, uc->fd, events);
}

void
UDPEventIO::process_event(int flags)
{
  // TODO: handle EVENTIO_ERROR
  if (flags & EVENTIO_READ) {
    ink_assert(_uc && _uc->mutex && _uc->continuation);
    ink_assert(_uc->refcount >= 1);
    _uh->open_list.in_or_enqueue(_uc); // due to the above race
    if (_uc->shouldDestroy()) {
      _uh->open_list.remove(_uc);
      _uc->Release();
    } else {
      udpNetInternal.udp_read_from_net(_uh, _uc);
    }
  } else {
    Debug("iocore_udp_main", "Unhandled epoll event: 0x%04x", flags);
  }
}
