/** @file

    The main file for proxy unit tests

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

#include <catch2/catch_test_macros.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include "tscore/Layout.h"
#include "iocore/eventsystem/EventSystem.h"
#include "records/RecordsConfig.h"
#include "iocore/utils/diags.i"

struct DiagnosticsListener : Catch::EventListenerBase {
  using EventListenerBase::EventListenerBase;

  void
  testRunStarting(Catch::TestRunInfo const & /* testRunInfo */) override
  {
    Layout::create();
    init_diags("", nullptr);
    RecProcessInit();
    LibRecordsConfigInit();
  }

  void
  testRunEnded(Catch::TestRunStats const & /* testRunStats */) override
  {
  }
};

CATCH_REGISTER_LISTENER(DiagnosticsListener);
