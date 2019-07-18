/** @file

    Catch-based unit tests for MIOBufferWriter class.

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

#define CATCH_CONFIG_RUNNER
#include "catch.hpp"

#include <cstdint>
#include <cstdlib>

#include "I_EventSystem.h"
#include "tscore/I_Layout.h"

#include "diags.i"
#include "I_MIOBufferWriter.h"

int
main(int argc, char *argv[])
{
  // global setup...
  Layout::create();
  init_diags("", nullptr);
  RecProcessInit(RECM_STAND_ALONE);

  ink_event_system_init(EVENT_SYSTEM_MODULE_PUBLIC_VERSION);
  eventProcessor.start(2);

  Thread *main_thread = new EThread;
  main_thread->set_specific();

  std::cout << "Pre-Catch" << std::endl;
  int result = Catch::Session().run(argc, argv);

  // global clean-up...

  exit(result);
}

std::string
genData(int numBytes)
{
  static std::uint8_t genData;

  std::string s(numBytes, ' ');

  for (int i{0}; i < numBytes; ++i) {
    s[i] = genData;
    genData += 7;
  }

  return s;
}

class InkAssertExcept
{
};

TEST_CASE("MIOBufferWriter", "[MIOBW]")
{
  MIOBuffer *theMIOBuffer = new_MIOBuffer(default_large_iobuffer_size);
  MIOBufferWriter bw(theMIOBuffer);
}
