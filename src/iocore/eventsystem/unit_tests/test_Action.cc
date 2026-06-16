/** @file

  Catch2 unit tests for the Action boundary contract.

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

#include "inkevent_test_fixtures.h"

#include <catch2/generators/catch_generators.hpp>

#include <cstdint>

using inkevent_test::CountingContinuation;
using inkevent_test::EventProcessorListener;

CATCH_REGISTER_LISTENER(EventProcessorListener)

namespace
{

class RecordingAction : public Action
{
public:
  using Action::operator=;

  std::atomic<int> derived_cancel_calls{0};

  void
  cancel(Continuation *c = nullptr) override
  {
    derived_cancel_calls.fetch_add(1, std::memory_order_release);
    Action::cancel(c);
  }
};

} // namespace

TEST_CASE("A default-constructed Action has a null continuation, a null mutex reference, and a false cancelled flag",
          "[inkevent][action]")
{
  Action a;
  REQUIRE(a.continuation == nullptr);
  REQUIRE(a.mutex.get() == nullptr);
  REQUIRE(a.cancelled == false);
}

TEST_CASE("Action::operator= with a non-null Continuation pins its observable post-conditions", "[inkevent][action]")
{
  Ptr<ProxyMutex>      mutex{new_ProxyMutex()};
  CountingContinuation cont{mutex.get()};
  int const            before = mutex->refcount();
  Action               a;

  Continuation *result = (a = &cont);

  SECTION("the operator returns its argument unchanged so callers can chain assignments")
  {
    REQUIRE(result == &cont);
  }

  SECTION("the Action stores the assigned Continuation pointer and adopts that Continuation's ProxyMutex")
  {
    REQUIRE(a.continuation == &cont);
    REQUIRE(a.mutex.get() == mutex.get());
  }

  SECTION("the Action retains an additional reference to the bound ProxyMutex")
  {
    REQUIRE(mutex->refcount() == before + 1);
  }

  SECTION("the Action's cancelled flag is unchanged by binding a Continuation")
  {
    REQUIRE(a.cancelled == false);
  }
}

TEST_CASE("Action::operator= with nullptr clears the bound continuation and drops the retained ProxyMutex reference",
          "[inkevent][action]")
{
  Ptr<ProxyMutex>      mutex{new_ProxyMutex()};
  CountingContinuation cont{mutex.get()};
  Action               a;
  a                      = &cont;
  int const before_clear = mutex->refcount();

  a = nullptr;

  REQUIRE(a.continuation == nullptr);
  REQUIRE(a.mutex.get() == nullptr);
  REQUIRE(mutex->refcount() == before_clear - 1);
}

TEST_CASE(
  "Reassigning Action::operator= to a different Continuation drops the previous ProxyMutex reference and adopts the new one",
  "[inkevent][action]")
{
  Ptr<ProxyMutex>      mutex1{new_ProxyMutex()};
  Ptr<ProxyMutex>      mutex2{new_ProxyMutex()};
  CountingContinuation cont1{mutex1.get()};
  CountingContinuation cont2{mutex2.get()};

  Action a;
  a                  = &cont1;
  int const ref1_one = mutex1->refcount();
  int const ref2_one = mutex2->refcount();

  a = &cont2;

  REQUIRE(a.continuation == &cont2);
  REQUIRE(a.mutex.get() == mutex2.get());
  REQUIRE(mutex1->refcount() == ref1_one - 1);
  REQUIRE(mutex2->refcount() == ref2_one + 1);
}

TEST_CASE(
  "Action::operator= with a Continuation that has a null mutex stores the Continuation pointer and leaves Action's mutex null",
  "[inkevent][action]")
{
  CountingContinuation cont{nullptr};
  Action               a;

  a = &cont;

  REQUIRE(a.continuation == &cont);
  REQUIRE(a.mutex.get() == nullptr);
}

TEST_CASE("Action::cancel flips the cancelled flag to true whether the bound Continuation is passed explicitly or omitted",
          "[inkevent][action]")
{
  bool const pass_continuation = GENERATE(true, false);

  Ptr<ProxyMutex>      mutex{new_ProxyMutex()};
  CountingContinuation cont{mutex.get()};
  Action               a;
  a = &cont;
  REQUIRE(a.cancelled == false);

  {
    SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
    a.cancel(pass_continuation ? &cont : nullptr);
  }

  REQUIRE(a.cancelled == true);
}

TEST_CASE("Action::cancel dispatches virtually so a derived Action's overridden cancel runs through an Action* base pointer",
          "[inkevent][action]")
{
  Ptr<ProxyMutex>      mutex{new_ProxyMutex()};
  CountingContinuation cont{mutex.get()};
  RecordingAction      derived;
  derived      = &cont;
  Action *base = &derived;

  {
    SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
    base->cancel(&cont);
  }

  REQUIRE(derived.derived_cancel_calls.load() == 1);
  REQUIRE(derived.cancelled == true);
}

TEST_CASE("Action::cancel_action sets the cancelled flag without invoking a derived class's overridden cancel",
          "[inkevent][action]")
{
  Ptr<ProxyMutex>      mutex{new_ProxyMutex()};
  CountingContinuation cont{mutex.get()};
  RecordingAction      derived;
  derived = &cont;

  {
    SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
    derived.cancel_action(&cont);
  }

  REQUIRE(derived.cancelled == true);
  REQUIRE(derived.derived_cancel_calls.load() == 0);
}

TEST_CASE("Destroying an Action that is bound to a Continuation drops its retained ProxyMutex reference", "[inkevent][action]")
{
  Ptr<ProxyMutex>      mutex{new_ProxyMutex()};
  CountingContinuation cont{mutex.get()};
  int const            before = mutex->refcount();

  {
    Action a;
    a = &cont;
    REQUIRE(mutex->refcount() == before + 1);
  }

  REQUIRE(mutex->refcount() == before);
}

TEST_CASE("MAKE_ACTION_RESULT encodes a small integer with the low bit set so callers can distinguish sentinels from real Actions",
          "[inkevent][action]")
{
  Action *s = MAKE_ACTION_RESULT(7);
  REQUIRE((reinterpret_cast<uintptr_t>(s) & 1u) == 1u);
  REQUIRE(reinterpret_cast<uintptr_t>(s) == ((7u << 1) + 1u));
}

TEST_CASE("ACTION_RESULT_DONE and ACTION_IO_ERROR are distinct sentinels with the low bit set", "[inkevent][action]")
{
  REQUIRE((reinterpret_cast<uintptr_t>(ACTION_RESULT_DONE) & 1u) == 1u);
  REQUIRE((reinterpret_cast<uintptr_t>(ACTION_IO_ERROR) & 1u) == 1u);
  REQUIRE(ACTION_RESULT_DONE != ACTION_IO_ERROR);
}

TEST_CASE("A Continuation scheduled into the future and cancelled before its deadline receives no dispatched event",
          "[inkevent][action][multithread]")
{
  Ptr<ProxyMutex>      target_mutex{new_ProxyMutex()};
  CountingContinuation target{target_mutex.get()};
  CountingContinuation barrier{new_ProxyMutex()};

  {
    SCOPED_MUTEX_LOCK(lock, target_mutex, this_ethread());
    Action *a = eventProcessor.schedule_in(&target, HRTIME_MSECONDS(20), ET_CALL);
    REQUIRE(a != nullptr);
    a->cancel(&target);
  }

  // Schedule a barrier with a strictly later deadline than target's. The
  // EThread cannot dispatch the barrier without first visiting target's slot
  // (PriorityEventQueue dequeues by deadline), so awaiting the barrier proves
  // the EThread actually ran past target's deadline — without which the
  // count() == 0 assertion below would be vacuous on a stalled EThread.
  Event *b = eventProcessor.schedule_in(&barrier, HRTIME_MSECONDS(150), ET_CALL);
  REQUIRE(b != nullptr);
  REQUIRE(barrier.wait_until_at_least(1));

  REQUIRE(target.count() == 0);
}
