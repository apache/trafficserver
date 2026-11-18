/** @file

  Unit tests for the HostDB continuations that wait for a pending DNS query.

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

#include "P_HostDB.h"
#include "records/RecordsConfig.h"
#include "tscore/Layout.h"

#include "iocore/utils/diags.i"

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

extern ClassAllocator<HostDBContinuation, false> hostDBContAllocator;

namespace
{
struct HostDBListener : Catch::EventListenerBase {
  using EventListenerBase::EventListenerBase;

  void
  testRunStarting(Catch::TestRunInfo const &) override
  {
    Layout::create();
    init_diags("", nullptr);
    RecProcessInit();
    LibRecordsConfigInit();
    ink_event_system_init(EVENT_SYSTEM_MODULE_PUBLIC_VERSION);
    (new EThread)->set_specific();
    hostDB.refcountcache = new RefCountCache<HostDBRecord>(1);
    hostDB.pending_dns   = new Queue<HostDBContinuation, Continuation::Link_link>[1];
  }
};
CATCH_REGISTER_LISTENER(HostDBListener);

struct Client : Continuation {
  int callbacks = 0;

  Client() : Continuation(new_ProxyMutex()) { SET_HANDLER(&Client::lookup_done); }

  int
  lookup_done(int, void *)
  {
    ++callbacks;
    return EVENT_DONE;
  }
};

HostDBContinuation *
queue_lookup(Client *client)
{
  HostDBHash hash;
  hash.set_host("example.com");
  hash.refresh();

  HostDBContinuation::Options opt;
  opt.cont = client;

  HostDBContinuation *c = hostDBContAllocator.alloc();
  c->init(hash, opt);
  c->set_check_pending_dns();
  SET_CONTINUATION_HANDLER(c, &HostDBContinuation::dnsPendingEvent);
  return c;
}

void
cancel_and_free(HostDBContinuation *c)
{
  SCOPED_MUTEX_LOCK(lock, c->mutex, this_ethread());
  c->action.cancel();
  c->handleEvent(EVENT_IMMEDIATE, nullptr);
}
} // namespace

TEST_CASE("A pending DNS waiter calls back one time on a timeout", "[hostdb]")
{
  Client              owner_client;
  Client              client;
  HostDBContinuation *owner  = queue_lookup(&owner_client);
  HostDBContinuation *waiter = queue_lookup(&client);
  Event               timeout;
  timeout.ethread = this_ethread();

  SECTION("the waiter is in the queue")
  {
    SCOPED_MUTEX_LOCK(lock, waiter->mutex, this_ethread());
    waiter->dnsPendingEvent(EVENT_INTERVAL, &timeout);
    CHECK(client.callbacks == 1);
  }

  SECTION("the DNS owner took the waiter out of the queue")
  {
    REQUIRE(hostDB.remove_from_pending_dns_for_hash(waiter->hash.hash, waiter));
    {
      SCOPED_MUTEX_LOCK(lock, waiter->mutex, this_ethread());
      waiter->dnsPendingEvent(EVENT_INTERVAL, &timeout);
    }
    CHECK(client.callbacks == 0);

    cancel_and_free(waiter);
    CHECK(client.callbacks == 0);
  }

  REQUIRE(hostDB.remove_from_pending_dns_for_hash(owner->hash.hash, owner));
  cancel_and_free(owner);
}

TEST_CASE("The DNS owner frees a cancelled pending DNS waiter", "[hostdb]")
{
  Client              owner_client;
  Client              client;
  HostDBContinuation *owner  = queue_lookup(&owner_client);
  HostDBContinuation *waiter = queue_lookup(&client);
  int                 refs   = client.mutex->refcount();

  {
    SCOPED_MUTEX_LOCK(lock, client.mutex, this_ethread());
    waiter->action.cancel();
  }
  owner->remove_and_trigger_pending_dns();

  // A freed waiter releases its reference to the mutex of the client.
  CHECK(client.mutex->refcount() == refs - 1);
  CHECK(client.callbacks == 0);

  cancel_and_free(owner);
}
