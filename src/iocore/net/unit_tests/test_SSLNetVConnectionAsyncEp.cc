/** @file

  Catch based unit test for the async-handshake eventfd teardown invariant
  in SSLNetVConnection.

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

#include "../P_SSLNetVConnection.h"
#include "../P_UnixPollDescriptor.h"
#include "iocore/net/EventIO.h"

#include <catch2/catch_test_macros.hpp>

#include <unistd.h>

// Grants the test access to the private async_ep member. A friend struct adds
// no callable surface to the production class, so it is safe in shipped builds.
struct SSLNetVConnectionAsyncEpTestAccess {
  static ReadWriteEventIO &
  async_ep(SSLNetVConnection *vc)
  {
    return vc->async_ep;
  }
};

// When OpenSSL returns SSL_ERROR_WANT_ASYNC during the TLS handshake,
// SSLNetVConnection registers async_ep on the poller with `this` as the
// EventIO target. free_thread() calls clear() immediately before the
// connection is returned to the allocator, so clear() must deregister the
// eventfd; otherwise the poller keeps a live registration pointing at memory
// that is about to be reused, which is the use-after-free this fix prevents.
TEST_CASE("SSLNetVConnection::clear stops a registered async-handshake eventfd")
{
  PollDescriptor pd;

  // Any pollable fd stands in for the OpenSSL async eventfd; a pipe read end
  // is a portable pollable fd. The deregistration the fix relies on is the
  // EPOLL_CTL_DEL in EventIO::stop(), which CI exercises on Linux.
  int fds[2] = {-1, -1};
  REQUIRE(pipe(fds) == 0);

  auto *vc = new SSLNetVConnection();
  auto &ep = SSLNetVConnectionAsyncEpTestAccess::async_ep(vc);

  // Arm the eventfd the same way the WANT_ASYNC path does. The NetEvent and
  // NetHandler pointers are only dereferenced when the poller dispatches an
  // event, which this test never triggers, so nullptr is sufficient here.
  ep.start(&pd, fds[0], nullptr, nullptr, EVENTIO_READ);
  REQUIRE(ep.event_loop == &pd);
  REQUIRE(ep.fd == fds[0]);

  vc->clear();

  // The fix: clear() deregisters the eventfd. EventIO::stop() nulls event_loop
  // after removing the registration from the poller.
  CHECK(ep.event_loop == nullptr);

  delete vc;
  close(fds[0]);
  close(fds[1]);
}

// The fix stops the eventfd in both do_io_close() and clear(), and on the
// normal close path both run. That double stop must be safe: free_thread()
// calls clear() after do_io_close() has already deregistered the eventfd.
// EventIO::stop() guards on event_loop, so the second call removes nothing.
TEST_CASE("SSLNetVConnection async-handshake eventfd teardown is idempotent")
{
  PollDescriptor pd;

  int fds[2] = {-1, -1};
  REQUIRE(pipe(fds) == 0);

  auto *vc = new SSLNetVConnection();
  auto &ep = SSLNetVConnectionAsyncEpTestAccess::async_ep(vc);

  ep.start(&pd, fds[0], nullptr, nullptr, EVENTIO_READ);
  REQUIRE(ep.event_loop == &pd);

  // do_io_close() frees the VC (via free_thread), so it cannot be driven
  // directly here; call stop() to mirror the deregistration it performs.
  ep.stop();
  REQUIRE(ep.event_loop == nullptr);

  // Second teardown via clear() (the free path) must be a no-op, not a second
  // deregistration of a now-stale fd.
  vc->clear();
  CHECK(ep.event_loop == nullptr);
  CHECK(ep.stop() == 0);

  delete vc;
  close(fds[0]);
  close(fds[1]);
}

// The common case: a connection that never returned SSL_ERROR_WANT_ASYNC never
// armed async_ep, so its fd stays -1. The `if (async_ep.fd >= 0)` guard in the
// fix must make clear() a no-op there -- no spurious deregistration, no crash.
TEST_CASE("SSLNetVConnection clear is a no-op when no async-handshake eventfd was armed")
{
  auto *vc = new SSLNetVConnection();
  auto &ep = SSLNetVConnectionAsyncEpTestAccess::async_ep(vc);

  // Default-constructed EventIO: never registered with a poller.
  REQUIRE(ep.fd < 0);
  REQUIRE(ep.event_loop == nullptr);

  vc->clear();

  // Nothing was armed, so nothing is deregistered and the guard is not entered.
  CHECK(ep.fd < 0);
  CHECK(ep.event_loop == nullptr);

  delete vc;
}
