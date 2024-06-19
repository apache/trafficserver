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

#include "iocore/net/AsyncSignalEventIO.h"
#include "iocore/eventsystem/EThread.h"

int
AsyncSignalEventIO::start(EventLoop l, int fd, int events)
{
  _fd = fd;
  return start_common(l, fd, events);
}

void
AsyncSignalEventIO::process_event(int /* flags ATS_UNUSED */)
{
  [[maybe_unused]] ssize_t ret;
#if HAVE_EVENTFD
  uint64_t counter;
  ret = read(_fd, &counter, sizeof(uint64_t));
  ink_assert(ret >= 0);
#else
  char dummy[1024];
  ret = read(_fd, &dummy[0], 1024);
  ink_assert(ret >= 0);
#endif
}
