/**
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

#include "iocore/dns/DNSEventIO.h"
#include "P_DNSConnection.h"

int
DNSEventIO::start(EventLoop l, int fd, int events)
{
  return start_common(l, fd, events);
}

void
DNSEventIO::process_event(int /* flags ATS_UNUSED */)
{
  _c.trigger(); // Make sure the DNSHandler for this con knows we triggered
  refresh(EVENTIO_READ);
}
