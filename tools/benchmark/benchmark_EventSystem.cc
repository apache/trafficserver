/** @file

  Micro Benchmark tool for Event System - requires Catch2 v2.9.0+

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

#define CATCH_CONFIG_ENABLE_BENCHMARKING
#define CATCH_CONFIG_RUNNER

#include "catch.hpp"

#include "iocore/eventsystem/Continuation.h"
#include "iocore/eventsystem/EventSystem.h"
#include "iocore/eventsystem/Lock.h"

#include "iocore/utils/diags.i"

#include "tscore/Layout.h"
#include "tscore/TSSystemState.h"

namespace
{
// Args
int nevents  = 1;
int nthreads = 1;

std::atomic<int> counter = 0;

struct Task : public Continuation {
  Task() : Continuation(new_ProxyMutex()) { SET_HANDLER(&Task::event_handler); }

  int
  event_handler(int /* event ATS_UNUSED */, Event * /* e ATS_UNUSED */)
  {
    ++counter;

    if (counter == nevents) {
      TSSystemState::shut_down_event_system();
    }

    return 0;
  }
};
} // namespace

TEST_CASE("event process benchmark", "")
{
  char name[64];
  snprintf(name, sizeof(name), "nevents = %d nthreads = %d", nevents, nthreads);

  BENCHMARK(name)
  {
    REQUIRE(!TSSystemState::is_initializing());

    for (int i = 0; i < nevents; ++i) {
      Task *t = new Task();
      eventProcessor.schedule_in(t, 0);
    }

    while (!TSSystemState::is_event_system_shut_down()) {
      sleep(1);
    }
  };
}

struct EventProcessorListener : Catch::TestEventListenerBase {
  using TestEventListenerBase::TestEventListenerBase;

  void
  testRunStarting(Catch::TestRunInfo const & /* testRunInfo ATS_UNUSED */) override
  {
    Layout::create();
    init_diags("", nullptr);
    RecProcessInit();

    ink_event_system_init(EVENT_SYSTEM_MODULE_PUBLIC_VERSION);
    eventProcessor.start(nthreads, 1048576); // Hardcoded stacksize at 1MB

    EThread *main_thread = new EThread;
    main_thread->set_specific();

    TSSystemState::initialization_done();
  }
};

CATCH_REGISTER_LISTENER(EventProcessorListener);

int
main(int argc, char *argv[])
{
  Catch::Session session;

  using namespace Catch::clara;

  auto cli = session.cli() | Opt(nevents, "n")["--ts-nevents"]("number of events (default: 1)\n") |
             Opt(nthreads, "n")["--ts-nthreads"]("number of ethreads (default: 1)\n");

  session.cli(cli);

  if (int res = session.applyCommandLine(argc, argv); res != 0) {
    return res;
  }

  return session.run();
}
