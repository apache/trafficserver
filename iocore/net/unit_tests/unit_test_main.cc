/** @file

  Catch based unit tests for libinknet

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

#include "I_EventSystem.h" // must be included before I_EThread.h
#include "I_EThread.h"
#include "I_Thread.h"
#include "P_SSLConfig.h"
#include "records/I_RecordsConfig.h"
#include "SSLAPIHooks.h"
#include "tscore/BaseLogFile.h"
#include "tscore/Diags.h"
#include "tscore/I_Layout.h"

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

inline static constexpr int test_threads{1};

class EventProcessorListener final : public Catch::TestEventListenerBase
{
public:
  using TestEventListenerBase::TestEventListenerBase;

  void
  testRunStarting(Catch::TestRunInfo const &testRunInfo) override
  {
    Layout::create();
    BaseLogFile *base_log_file = new BaseLogFile("stderr");
    DiagsPtr::set(new Diags(testRunInfo.name, "" /* tags */, "" /* actions */, base_log_file));
    RecProcessInit();
    LibRecordsConfigInit();

    ink_event_system_init(EVENT_SYSTEM_MODULE_PUBLIC_VERSION);
    eventProcessor.start(test_threads);

    EThread *main_thread = new EThread;
    main_thread->set_specific();

    SSLConfig::startup();
    init_global_ssl_hooks();
  }

  void
  testRunEnded(Catch::TestRunStats const & /* testRunStats */) override
  {
  }
};

CATCH_REGISTER_LISTENER(EventProcessorListener);
