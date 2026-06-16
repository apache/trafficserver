/** @file

  Catch2 unit tests for the Continuation boundary contract.

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

using inkevent_test::AtomicFlag;
using inkevent_test::CountingContinuation;
using inkevent_test::EventProcessorListener;

CATCH_REGISTER_LISTENER(EventProcessorListener)

namespace
{

class FlagContinuation : public Continuation
{
public:
  explicit FlagContinuation(ProxyMutex *m) : Continuation(m) { SET_HANDLER(&FlagContinuation::on_event); }

  AtomicFlag flag;
  int        last_event = -1;
  void      *last_data  = nullptr;

private:
  int
  on_event(int event, void *data)
  {
    last_event = event;
    last_data  = data;
    flag.set();
    return 0;
  }
};

class TwoHandlerContinuation : public Continuation
{
public:
  explicit TwoHandlerContinuation(ProxyMutex *m) : Continuation(m) { SET_HANDLER(&TwoHandlerContinuation::first); }

  std::atomic<int> first_calls{0};
  std::atomic<int> second_calls{0};

  int
  first(int /* event */, void * /* data */)
  {
    first_calls.fetch_add(1, std::memory_order_release);
    SET_HANDLER(&TwoHandlerContinuation::second);
    return 0;
  }

  int
  second(int /* event */, void * /* data */)
  {
    second_calls.fetch_add(1, std::memory_order_release);
    return 0;
  }
};

} // namespace

TEST_CASE("Continuation constructed with a raw ProxyMutex pointer retains the mutex and leaves all other fields in their "
          "documented initial state",
          "[inkevent][continuation]")
{
  Ptr<ProxyMutex>  mutex{new_ProxyMutex()};
  FlagContinuation cont{mutex.get()};

  REQUIRE(cont.getMutex() == mutex.get());
  REQUIRE(cont.getThreadAffinity() == nullptr);
  REQUIRE(cont.handler != nullptr);
}

TEST_CASE("Continuation constructed with a null ProxyMutex pointer reports a null mutex via getMutex", "[inkevent][continuation]")
{
  FlagContinuation cont{nullptr};

  REQUIRE(cont.getMutex() == nullptr);
  REQUIRE(cont.getThreadAffinity() == nullptr);
}

TEST_CASE("Continuation::getMutex returns the same raw pointer for repeated calls without changing the held reference count",
          "[inkevent][continuation]")
{
  Ptr<ProxyMutex> mutex{new_ProxyMutex()};
  int const       initial_refcount = mutex->refcount();

  {
    FlagContinuation cont{mutex.get()};
    ProxyMutex      *first  = cont.getMutex();
    ProxyMutex      *second = cont.getMutex();

    REQUIRE(first == second);
    REQUIRE(first == mutex.get());
    REQUIRE(mutex->refcount() == initial_refcount + 1);
  }

  REQUIRE(mutex->refcount() == initial_refcount);
}

TEST_CASE("Continuation::setThreadAffinity returns true and stores the EThread when the argument is non-null",
          "[inkevent][continuation]")
{
  Ptr<ProxyMutex>  mutex{new_ProxyMutex()};
  FlagContinuation cont{mutex.get()};
  EThread         *me = this_ethread();
  REQUIRE(me != nullptr);

  bool const accepted = cont.setThreadAffinity(me);

  REQUIRE(accepted);
  REQUIRE(cont.getThreadAffinity() == me);
}

TEST_CASE("Continuation::setThreadAffinity returns false and leaves the affinity unchanged when called with a null EThread pointer",
          "[inkevent][continuation]")
{
  Ptr<ProxyMutex>  mutex{new_ProxyMutex()};
  FlagContinuation cont{mutex.get()};
  EThread         *me = this_ethread();
  REQUIRE(cont.setThreadAffinity(me));

  bool const accepted = cont.setThreadAffinity(nullptr);

  REQUIRE_FALSE(accepted);
  REQUIRE(cont.getThreadAffinity() == me);
}

TEST_CASE("Continuation::clearThreadAffinity restores the no-preference state observable through getThreadAffinity",
          "[inkevent][continuation]")
{
  Ptr<ProxyMutex>  mutex{new_ProxyMutex()};
  FlagContinuation cont{mutex.get()};
  cont.setThreadAffinity(this_ethread());
  REQUIRE(cont.getThreadAffinity() != nullptr);

  cont.clearThreadAffinity();

  REQUIRE(cont.getThreadAffinity() == nullptr);
}

TEST_CASE("Continuation::handleEvent forwards the event code and data pointer to the installed handler", "[inkevent][continuation]")
{
  Ptr<ProxyMutex>  mutex{new_ProxyMutex()};
  FlagContinuation cont{mutex.get()};

  int payload = 42;
  int event   = 7;
  {
    SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
    cont.handleEvent(event, &payload);
  }

  REQUIRE(cont.flag.is_set());
  REQUIRE(cont.last_event == event);
  REQUIRE(cont.last_data == static_cast<void *>(&payload));
}

TEST_CASE("Continuation::handleEvent uses CONTINUATION_EVENT_NONE and a null data pointer when called with no arguments",
          "[inkevent][continuation]")
{
  Ptr<ProxyMutex>  mutex{new_ProxyMutex()};
  FlagContinuation cont{mutex.get()};

  {
    SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
    cont.handleEvent();
  }

  REQUIRE(cont.last_event == CONTINUATION_EVENT_NONE);
  REQUIRE(cont.last_data == nullptr);
}

TEST_CASE("SET_HANDLER replaces the active handler so the next handleEvent dispatch invokes the new method",
          "[inkevent][continuation]")
{
  Ptr<ProxyMutex>        mutex{new_ProxyMutex()};
  TwoHandlerContinuation cont{mutex.get()};

  {
    SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
    cont.handleEvent();
    cont.handleEvent();
  }

  REQUIRE(cont.first_calls.load() == 1);
  REQUIRE(cont.second_calls.load() == 1);
}

TEST_CASE("SET_CONTINUATION_HANDLER installs a handler on a peer Continuation pointed to by the caller", "[inkevent][continuation]")
{
  Ptr<ProxyMutex>        mutex{new_ProxyMutex()};
  TwoHandlerContinuation cont{mutex.get()};
  REQUIRE(cont.handler != nullptr);

  TwoHandlerContinuation *peer = &cont;
  SET_CONTINUATION_HANDLER(peer, &TwoHandlerContinuation::second);

  {
    SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
    cont.handleEvent();
  }

  REQUIRE(cont.second_calls.load() == 1);
  REQUIRE(cont.first_calls.load() == 0);
}

TEST_CASE("Scheduling a CountingContinuation onto eventProcessor invokes its handler on a dispatching EThread",
          "[inkevent][continuation][multithread]")
{
  CountingContinuation cont{new_ProxyMutex()};
  Event               *e = eventProcessor.schedule_imm(&cont, ET_CALL);
  REQUIRE(e != nullptr);

  REQUIRE(cont.wait_until_at_least(1));
  REQUIRE(cont.count() >= 1);
}

TEST_CASE("Continuation::handleEvent returns the value the handler returns", "[inkevent][continuation]")
{
  Ptr<ProxyMutex> mutex{new_ProxyMutex()};

  struct Echo : public Continuation {
    int echo_value;
    explicit Echo(ProxyMutex *m, int v) : Continuation(m), echo_value(v) { SET_HANDLER(&Echo::handler_method); }
    int
    handler_method(int /* event */, void * /* data */)
    {
      return echo_value;
    }
  };

  Echo a{mutex.get(), CONTINUATION_DONE};
  Echo b{mutex.get(), CONTINUATION_CONT};

  SCOPED_MUTEX_LOCK(lock, mutex, this_ethread());
  REQUIRE(a.handleEvent(0, nullptr) == CONTINUATION_DONE);
  REQUIRE(b.handleEvent(0, nullptr) == CONTINUATION_CONT);
}
