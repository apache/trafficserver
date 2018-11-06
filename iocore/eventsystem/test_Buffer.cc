/** @file

  A brief file description

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

#include "I_EventSystem.h"
#include "tscore/I_Layout.h"
#include "tscore/ink_string.h"

#include "diags.i"

#define TEST_TIME_SECOND 60
#define TEST_THREADS 2

int
main(int /* argc ATS_UNUSED */, const char * /* argv ATS_UNUSED */ [])
{
  RecModeT mode_type = RECM_STAND_ALONE;

  Layout::create();
  init_diags("", nullptr);
  RecProcessInit(mode_type);

  ink_event_system_init(EVENT_SYSTEM_MODULE_PUBLIC_VERSION);
  eventProcessor.start(TEST_THREADS);

  Thread *main_thread = new EThread;
  main_thread->set_specific();

  for (unsigned i = 0; i < 100; ++i) {
    MIOBuffer *b1                       = new_MIOBuffer(default_large_iobuffer_size);
    IOBufferReader *b1reader ATS_UNUSED = b1->alloc_reader();
    b1->fill(b1->write_avail());

    MIOBuffer *b2                       = new_MIOBuffer(default_large_iobuffer_size);
    IOBufferReader *b2reader ATS_UNUSED = b2->alloc_reader();
    b2->fill(b2->write_avail());

    // b1->write(b2reader, 2*1024);

    free_MIOBuffer(b2);
    free_MIOBuffer(b1);
  }

  exit(0);
}
