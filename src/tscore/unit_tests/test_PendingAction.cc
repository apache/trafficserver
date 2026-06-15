/** @file

  Unit tests for PendingAction.

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
  See the NOTICE file distributed with this work for additional information regarding copyright
  ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance with the License.  You may obtain a
  copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software distributed under the License
  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
  or implied. See the License for the specific language governing permissions and limitations under
  the License.
 */

#include "iocore/eventsystem/Action.h"
#include "tscore/PendingAction.h"

#include "catch.hpp"

namespace
{
// Action subclass that records cancel() calls so the tests can assert
// whether the PendingAction operation cancelled the action or not.
class TestAction : public Action
{
public:
  void
  cancel(Continuation *c = nullptr) override
  {
    Action::cancel(c);
    cancel_count++;
  }
  int cancel_count = 0;
};
} // namespace

TEST_CASE("PendingAction::clear_if_action_is clears only the matching action", "[PendingAction]")
{
  // Declare the Action before PendingAction so it outlives pa during stack
  // unwinding on a REQUIRE failure - PendingAction's destructor calls
  // cancel() on whatever it still holds.
  TestAction    actionA;
  PendingAction pa;

  pa = &actionA;
  REQUIRE(pa.get() == &actionA);

  REQUIRE(pa.clear_if_action_is(&actionA));
  REQUIRE(pa.empty());
  REQUIRE(actionA.cancel_count == 0); // clear_if_action_is must not cancel
}

TEST_CASE("PendingAction::clear_if_action_is does not touch a non-matching action", "[PendingAction]")
{
  // Regression for the CAS-race where compare_exchange_strong overwrote its
  // expected argument on failure and the surrounding loop then cleared the
  // *new* pending_action as if it were the one the caller asked about.
  // Even in single-threaded use, calling clear_if_action_is with the wrong
  // pointer must not touch what is currently pending.
  TestAction    actionA;
  TestAction    actionB;
  PendingAction pa;

  pa = &actionA;
  REQUIRE(pa.get() == &actionA);

  REQUIRE_FALSE(pa.clear_if_action_is(&actionB));
  REQUIRE(pa.get() == &actionA); // the unrelated action is still pending
  REQUIRE(actionB.cancel_count == 0);

  // Clear the pending action so PendingAction's destructor does not call
  // cancel() on it during teardown.
  REQUIRE(pa.clear_if_action_is(&actionA));
}

TEST_CASE("PendingAction::clear_if_action_is on a null action is a no-op", "[PendingAction]")
{
  TestAction    actionA;
  PendingAction pa;

  pa = &actionA;
  REQUIRE_FALSE(pa.clear_if_action_is(nullptr));
  REQUIRE(pa.get() == &actionA);
  REQUIRE(pa.clear_if_action_is(&actionA));
}
