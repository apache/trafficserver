/** @file

  Catch based unit tests for EventSystem

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

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "I_EventSystem.h"
#include "tscore/I_Layout.h"
#include "tscore/TSSystemState.h"

#include "diags.i"

#define TEST_TIME_SECOND 60
#define TEST_THREADS 2

TEST_CASE("EventSystem", "[iocore]")
{
  static int count;

  struct alarm_printer : public Continuation {
    alarm_printer(ProxyMutex *m) : Continuation(m) { SET_HANDLER(&alarm_printer::dummy_function); }

    int
    dummy_function(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
    {
      ink_atomic_increment((int *)&count, 1);

      EThread *e = this_ethread();
      printf("thread=%d (%p) count = %d\n", e->id, e, count);

      return 0;
    }
  };

  struct process_killer : public Continuation {
    process_killer(ProxyMutex *m) : Continuation(m) { SET_HANDLER(&process_killer::kill_function); }

    int
    kill_function(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
    {
      EThread *e = this_ethread();
      printf("thread=%d (%p) count is %d\n", e->id, e, count);

      REQUIRE(count > 0);
      REQUIRE(count <= TEST_TIME_SECOND * TEST_THREADS);

      TSSystemState::shut_down_event_system();

      return 0;
    }
  };

  alarm_printer *alrm    = new alarm_printer(new_ProxyMutex());
  process_killer *killer = new process_killer(new_ProxyMutex());
  eventProcessor.schedule_in(killer, HRTIME_SECONDS(10));
  eventProcessor.schedule_every(alrm, HRTIME_SECONDS(1));

  while (!TSSystemState::is_event_system_shut_down()) {
    sleep(1);
  }
}

struct EventProcessorListener : Catch::TestEventListenerBase {
  using TestEventListenerBase::TestEventListenerBase;

  void
  testRunStarting(Catch::TestRunInfo const &testRunInfo) override
  {
    Layout::create();
    init_diags("", nullptr);
    RecProcessInit(RECM_STAND_ALONE);

    ink_event_system_init(EVENT_SYSTEM_MODULE_PUBLIC_VERSION);
    eventProcessor.start(TEST_THREADS, 1048576); // Hardcoded stacksize at 1MB

    EThread *main_thread = new EThread;
    main_thread->set_specific();
  }
};

CATCH_REGISTER_LISTENER(EventProcessorListener);
