/** @file

  Tests for kqueue registration teardown when a socket is migrated.

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

#include "../P_UnixPollDescriptor.h"
#include "iocore/net/EventIO.h"
#include "iocore/net/ReadWriteEventIO.h"

#include <catch2/catch_test_macros.hpp>

#include <cerrno>
#include <unistd.h>

#if TS_USE_KQUEUE
namespace
{
struct TestEventIO : EventIO {
  using EventIO::start_common;
  void
  process_event(int) override
  {
  }
};
} // namespace

TEST_CASE("Migrating an open socket removes both old kqueue filters")
{
  PollDescriptor pd;
  TestEventIO    ep;
  int            fds[2];

  REQUIRE(pipe(fds) == 0);
  REQUIRE(ep.start_common(&pd, fds[0], EVENTIO_READ | EVENTIO_WRITE) == 0);
  REQUIRE(ep.stop_for_migration() == 0);
  CHECK(ep.event_loop == nullptr);

  // The descriptor is still open, as it would be after moving a connection.
  struct kevent changes[2];
  struct kevent receipts[2];

  EV_SET(&changes[0], fds[0], EVFILT_READ, EV_DELETE | EV_RECEIPT, 0, 0, nullptr);
  EV_SET(&changes[1], fds[0], EVFILT_WRITE, EV_DELETE | EV_RECEIPT, 0, 0, nullptr);
  REQUIRE(kevent(pd.kqueue_fd, changes, 2, receipts, 2, nullptr) == 2);
  CHECK(receipts[0].data == ENOENT);
  CHECK(receipts[1].data == ENOENT);

  close(fds[0]);
  close(fds[1]);
}

TEST_CASE("A fetched event for a migrated socket is ignored")
{
  PollDescriptor   pd;
  ReadWriteEventIO ep;
  int              fds[2];

  REQUIRE(pipe(fds) == 0);
  REQUIRE(ep.start(&pd, fds[0], nullptr, nullptr, EVENTIO_READ) == 0);
  REQUIRE(ep.stop_for_migration() == 0);

  // A fetched event may still carry this EventIO after EV_DELETE. If processed,
  // its NetEvent and NetHandler would be stale (nullptr in this test).
  ep.process_event(EVENTIO_READ);

  close(fds[0]);
  close(fds[1]);
}
#endif
