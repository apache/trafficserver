/** @file

  Catch2 unit tests for the Lock.h mutex acquisition macros.

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

#define CATCH_CONFIG_THREAD_SAFE_ASSERTIONS
#include "inkevent_test_fixtures.h"

using inkevent_test::AtomicFlag;
using inkevent_test::EventProcessorListener;

CATCH_REGISTER_LISTENER(EventProcessorListener)

namespace
{

class HoldOnEThread : public Continuation
{
public:
  HoldOnEThread(ProxyMutex *self_mutex, Ptr<ProxyMutex> &target) : Continuation(self_mutex), target_mutex(target)
  {
    SET_HANDLER(&HoldOnEThread::on_event);
  }

  // In case of an exception in a thread that would have set release, we set
  // it here in order to unfreeze any threads that may be waiting on done.
  ~HoldOnEThread() { release.set(); }

  Ptr<ProxyMutex> target_mutex;
  AtomicFlag      held;
  AtomicFlag      release;
  AtomicFlag      done;

private:
  int
  on_event(int /* event ATS_UNUSED */, void * /* data ATS_UNUSED */)
  {
    {
      SCOPED_MUTEX_LOCK(guard, target_mutex, this_ethread());
      held.set();
      // Non-fatal so that done gets set.
      CHECK(release.wait_until_set());
    }
    done.set();
    return 0;
  }
};

} // namespace

TEST_CASE("MUTEX_TRY_LOCK on an unheld ProxyMutex constructs a guard whose is_locked() reports the successful acquisition",
          "[inkevent][lock]")
{
  Ptr<ProxyMutex> m{new_ProxyMutex()};
  EThread        *t = this_ethread();

  MUTEX_TRY_LOCK(guard, m, t);

  REQUIRE(guard.is_locked());
  REQUIRE(m->thread_holding == t);
  REQUIRE(m->nthread_holding == 1);
}

TEST_CASE("MUTEX_TRY_LOCK against a contended ProxyMutex constructs a guard whose is_locked() reports the failed acquisition",
          "[inkevent][lock][multithread]")
{
  Ptr<ProxyMutex> contended{new_ProxyMutex()};
  Ptr<ProxyMutex> cont_self{new_ProxyMutex()};
  HoldOnEThread   holder{cont_self.get(), contended};

  REQUIRE(eventProcessor.schedule_imm(&holder, ET_CALL) != nullptr);
  REQUIRE(holder.held.wait_until_set());

  EThread *t = this_ethread();
  MUTEX_TRY_LOCK(guard, contended, t);

  REQUIRE_FALSE(guard.is_locked());

  holder.release.set();
  REQUIRE(holder.done.wait_until_set());
}

TEST_CASE("MUTEX_TRY_LOCK by the holding thread is reentrant and returns a guard reporting is_locked() == true", "[inkevent][lock]")
{
  Ptr<ProxyMutex> m{new_ProxyMutex()};
  EThread        *t = this_ethread();

  MUTEX_TRY_LOCK(outer, m, t);
  REQUIRE(outer.is_locked());
  REQUIRE(m->nthread_holding == 1);

  {
    MUTEX_TRY_LOCK(inner, m, t);
    REQUIRE(inner.is_locked());
    REQUIRE(m->nthread_holding == 2);
  }

  REQUIRE(m->nthread_holding == 1);
  REQUIRE(m->thread_holding == t);
}

TEST_CASE("A MUTEX_TRY_LOCK guard releases the lock at scope exit, leaving the ProxyMutex unheld", "[inkevent][lock]")
{
  Ptr<ProxyMutex> m{new_ProxyMutex()};
  EThread        *t = this_ethread();

  {
    MUTEX_TRY_LOCK(guard, m, t);
    REQUIRE(guard.is_locked());
  }

  REQUIRE(m->nthread_holding == 0);
  REQUIRE(m->thread_holding == nullptr);
}

TEST_CASE("MUTEX_RELEASE on a MUTEX_TRY_LOCK guard releases the lock early and flips is_locked() to false", "[inkevent][lock]")
{
  Ptr<ProxyMutex> m{new_ProxyMutex()};
  EThread        *t = this_ethread();

  MUTEX_TRY_LOCK(guard, m, t);
  REQUIRE(guard.is_locked());

  MUTEX_RELEASE(guard);

  REQUIRE_FALSE(guard.is_locked());
  REQUIRE(m->nthread_holding == 0);
  REQUIRE(m->thread_holding == nullptr);
}

TEST_CASE("MUTEX_RELEASE invoked twice on the same MUTEX_TRY_LOCK guard is a no-op on the second call", "[inkevent][lock]")
{
  Ptr<ProxyMutex> m{new_ProxyMutex()};
  EThread        *t = this_ethread();

  MUTEX_TRY_LOCK(guard, m, t);
  MUTEX_RELEASE(guard);
  REQUIRE_FALSE(guard.is_locked());

  MUTEX_RELEASE(guard);

  REQUIRE_FALSE(guard.is_locked());
  REQUIRE(m->nthread_holding == 0);
}

TEST_CASE("SCOPED_MUTEX_LOCK on an unheld ProxyMutex acquires the lock during construction", "[inkevent][lock]")
{
  Ptr<ProxyMutex> m{new_ProxyMutex()};
  EThread        *t = this_ethread();

  SCOPED_MUTEX_LOCK(guard, m, t);

  REQUIRE(m->thread_holding == t);
  REQUIRE(m->nthread_holding == 1);
}

TEST_CASE("A SCOPED_MUTEX_LOCK guard releases the lock when its enclosing scope ends", "[inkevent][lock]")
{
  Ptr<ProxyMutex> m{new_ProxyMutex()};
  EThread        *t = this_ethread();

  {
    SCOPED_MUTEX_LOCK(guard, m, t);
    REQUIRE(m->nthread_holding == 1);
  }

  REQUIRE(m->nthread_holding == 0);
  REQUIRE(m->thread_holding == nullptr);
}

TEST_CASE("SCOPED_MUTEX_LOCK is reentrant when the calling EThread already holds the lock", "[inkevent][lock]")
{
  Ptr<ProxyMutex> m{new_ProxyMutex()};
  EThread        *t = this_ethread();

  SCOPED_MUTEX_LOCK(outer, m, t);
  REQUIRE(m->nthread_holding == 1);

  {
    SCOPED_MUTEX_LOCK(inner, m, t);
    REQUIRE(m->nthread_holding == 2);
    REQUIRE(m->thread_holding == t);
  }

  REQUIRE(m->nthread_holding == 1);
  REQUIRE(m->thread_holding == t);
}

TEST_CASE("MUTEX_RELEASE on a SCOPED_MUTEX_LOCK guard releases the lock early and renders the destructor a no-op",
          "[inkevent][lock]")
{
  Ptr<ProxyMutex> m{new_ProxyMutex()};
  EThread        *t = this_ethread();

  {
    SCOPED_MUTEX_LOCK(guard, m, t);
    MUTEX_RELEASE(guard);

    REQUIRE(m->nthread_holding == 0);
    REQUIRE(m->thread_holding == nullptr);
  }

  REQUIRE(m->nthread_holding == 0);
  REQUIRE(m->thread_holding == nullptr);
}

TEST_CASE("MUTEX_TAKE_LOCK acquires an unheld ProxyMutex and records the calling thread as its holder", "[inkevent][lock]")
{
  Ptr<ProxyMutex> m{new_ProxyMutex()};
  EThread        *t = this_ethread();

  MUTEX_TAKE_LOCK(m, t);

  REQUIRE(m->thread_holding == t);
  REQUIRE(m->nthread_holding == 1);

  MUTEX_UNTAKE_LOCK(m, t);
}

TEST_CASE("MUTEX_TAKE_LOCK by the holding thread is reentrant and increments the reentry count without blocking",
          "[inkevent][lock]")
{
  Ptr<ProxyMutex> m{new_ProxyMutex()};
  EThread        *t = this_ethread();

  MUTEX_TAKE_LOCK(m, t);
  MUTEX_TAKE_LOCK(m, t);

  REQUIRE(m->nthread_holding == 2);
  REQUIRE(m->thread_holding == t);

  MUTEX_UNTAKE_LOCK(m, t);
  MUTEX_UNTAKE_LOCK(m, t);

  REQUIRE(m->nthread_holding == 0);
  REQUIRE(m->thread_holding == nullptr);
}

TEST_CASE("After the holding EThread fully releases a contended ProxyMutex, MUTEX_TRY_LOCK on the main thread succeeds",
          "[inkevent][lock][multithread]")
{
  Ptr<ProxyMutex> contended{new_ProxyMutex()};
  Ptr<ProxyMutex> cont_self{new_ProxyMutex()};
  HoldOnEThread   holder{cont_self.get(), contended};

  REQUIRE(eventProcessor.schedule_imm(&holder, ET_CALL) != nullptr);
  REQUIRE(holder.held.wait_until_set());

  holder.release.set();
  REQUIRE(holder.done.wait_until_set());

  EThread *t = this_ethread();
  MUTEX_TRY_LOCK(guard, contended, t);

  REQUIRE(guard.is_locked());
  REQUIRE(contended->thread_holding == t);
}

TEST_CASE("WEAK_SCOPED_MUTEX_LOCK on a non-null ProxyMutex acquires the lock during construction", "[inkevent][lock]")
{
  Ptr<ProxyMutex> m{new_ProxyMutex()};
  EThread        *t = this_ethread();

  WEAK_SCOPED_MUTEX_LOCK(guard, m, t);

  REQUIRE(m->thread_holding == t);
  REQUIRE(m->nthread_holding == 1);
}

TEST_CASE("A WEAK_SCOPED_MUTEX_LOCK guard releases its lock when its enclosing scope ends", "[inkevent][lock]")
{
  Ptr<ProxyMutex> m{new_ProxyMutex()};
  EThread        *t = this_ethread();

  {
    WEAK_SCOPED_MUTEX_LOCK(guard, m, t);
    REQUIRE(m->nthread_holding == 1);
  }

  REQUIRE(m->nthread_holding == 0);
  REQUIRE(m->thread_holding == nullptr);
}

TEST_CASE("WEAK_MUTEX_TRY_LOCK on an unheld ProxyMutex constructs a guard whose is_locked() reports the successful acquisition",
          "[inkevent][lock]")
{
  Ptr<ProxyMutex> m{new_ProxyMutex()};
  EThread        *t = this_ethread();

  WEAK_MUTEX_TRY_LOCK(guard, m, t);

  REQUIRE(guard.is_locked());
  REQUIRE(m->thread_holding == t);
  REQUIRE(m->nthread_holding == 1);
}

TEST_CASE("WEAK_MUTEX_TRY_LOCK against a contended ProxyMutex constructs a guard whose is_locked() reports the failed acquisition",
          "[inkevent][lock][multithread]")
{
  Ptr<ProxyMutex> contended{new_ProxyMutex()};
  Ptr<ProxyMutex> cont_self{new_ProxyMutex()};
  HoldOnEThread   holder{cont_self.get(), contended};

  REQUIRE(eventProcessor.schedule_imm(&holder, ET_CALL) != nullptr);
  REQUIRE(holder.held.wait_until_set());

  EThread *t = this_ethread();
  WEAK_MUTEX_TRY_LOCK(guard, contended, t);

  REQUIRE_FALSE(guard.is_locked());

  holder.release.set();
  REQUIRE(holder.done.wait_until_set());
}

TEST_CASE("WEAK_MUTEX_TRY_LOCK on a null Ptr<ProxyMutex> reports is_locked() == true", "[inkevent][lock]")
{
  Ptr<ProxyMutex> empty;
  EThread        *t = this_ethread();

  REQUIRE(empty.get() == nullptr);

  WEAK_MUTEX_TRY_LOCK(guard, empty, t);

  REQUIRE(guard.is_locked());
}

TEST_CASE("MUTEX_RELEASE on a null WEAK_MUTEX_TRY_LOCK guard clears is_locked() without unlocking a mutex", "[inkevent][lock]")
{
  Ptr<ProxyMutex> empty;
  EThread        *t = this_ethread();

  WEAK_MUTEX_TRY_LOCK(guard, empty, t);
  REQUIRE(guard.is_locked());

  MUTEX_RELEASE(guard);

  REQUIRE_FALSE(guard.is_locked());
}
