/** @file

  Catch based unit tests on EventSystem

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

namespace
{
constexpr int TEST_THREADS = 2;
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
    eventProcessor.start(TEST_THREADS);

    EThread *main_thread = new EThread;
    main_thread->set_specific();
  }
};

CATCH_REGISTER_LISTENER(EventProcessorListener);
