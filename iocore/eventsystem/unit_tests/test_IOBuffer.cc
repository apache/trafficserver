/** @file

  Catch based unit tests for IOBuffer

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

#include "diags.i"

#define TEST_THREADS 1

TEST_CASE("MIOBuffer", "[iocore]")
{
  for (unsigned i = 0; i < 100; ++i) {
    MIOBuffer *b1            = new_MIOBuffer(default_small_iobuffer_size);
    int64_t len1             = b1->write_avail();
    IOBufferReader *b1reader = b1->alloc_reader();
    b1->fill(len1);
    CHECK(b1reader->read_avail() == len1);

    MIOBuffer *b2            = new_MIOBuffer(default_large_iobuffer_size);
    int64_t len2             = b1->write_avail();
    IOBufferReader *b2reader = b2->alloc_reader();
    b2->fill(len2);
    CHECK(b2reader->read_avail() == len2);

    free_MIOBuffer(b2);
    free_MIOBuffer(b1);
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
    eventProcessor.start(TEST_THREADS);

    EThread *main_thread = new EThread;
    main_thread->set_specific();
  }
};

CATCH_REGISTER_LISTENER(EventProcessorListener);
