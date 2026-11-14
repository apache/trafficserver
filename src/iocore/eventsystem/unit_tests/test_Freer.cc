/** @file

  Catch2 unit tests for the deferred release helpers in Freer.h.

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

#include "iocore/eventsystem/Freer.h"
#include "tscore/Ptr.h"

using inkevent_test::EventProcessorListener;

CATCH_REGISTER_LISTENER(EventProcessorListener)

namespace
{

class Tracked : public RefCountObjInHeap
{
public:
  explicit Tracked(bool &deleted) : _deleted(deleted) {}
  ~Tracked() override { _deleted = true; }

private:
  bool &_deleted;
};

// Run the continuation that new_Derefer schedules, without waiting for the delay.
void
run_derefer(Tracked *p)
{
  auto *cont = new DereferContinuation<Tracked>(p);
  cont->handleEvent(EVENT_IMMEDIATE, nullptr);
}

} // namespace

TEST_CASE("DereferContinuation deletes the object when it drops the last reference", "[inkevent][freer]")
{
  bool  deleted = false;
  auto *p       = new Tracked(deleted);
  p->refcount_inc();

  run_derefer(p);

  REQUIRE(deleted);
}

TEST_CASE("DereferContinuation drops exactly one reference and keeps an object that is still referenced", "[inkevent][freer]")
{
  bool  deleted = false;
  auto *p       = new Tracked(deleted);
  p->refcount_inc();
  p->refcount_inc();

  run_derefer(p);

  REQUIRE_FALSE(deleted);
  REQUIRE(p->refcount() == 1);

  if (p->refcount_dec() == 0) {
    delete p;
  }
  REQUIRE(deleted);
}
