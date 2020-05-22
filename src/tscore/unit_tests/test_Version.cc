/** @file

    Tokenizer tests.

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

#include "tscore/I_Version.h"
#include "tscore/ink_config.h"

#include <cstdio>
#include <cstring>

#include <catch.hpp>

TEST_CASE("AppVersionInfo", "[libts][version]")
{
  AppVersionInfo info;

  const char *errMsgFormat = "wrong build number, expected '%s', got '%s'\n";
  const char *bench[][3]   = {// date, time, resulting build number
                            {"Oct  4 1957", "19:28:34", BUILD_NUMBER},
                            {"Oct  4 1957", "19:28:34", "100419"},
                            {"Apr  4 1957", "09:08:04", "040409"},
                            {" 4 Apr 1957", "09:08:04", "??????"},
                            {"Apr  4 1957", "09-08-04", "??????"}};

  int benchSize = sizeof(bench) / sizeof(bench[0]);

  if (0 != std::strlen(BUILD_NUMBER)) {
    // Since BUILD_NUMBER is defined by a #define directive, it is not
    // possible to change the version value from inside the regression test.
    // If not empty BUILD_NUMBER overrides any result, in this case run only
    // this test (the rest will always fail).
    info.setup("Apache Traffic Server", "traffic_server", "5.2.1", bench[0][0], bench[0][1], "build_slave", "builder", "");
    if (0 != std::strcmp(info.BldNumStr, bench[0][2])) {
      std::printf(errMsgFormat, bench[0][2], info.BldNumStr);
      CHECK(false);
    }
  } else {
    for (int i = 1; i < benchSize; i++) {
      info.setup("Apache Traffic Server", "traffic_server", "5.2.1", bench[i][0], bench[i][1], "build_slave", "builder", "");
      if (0 != std::strcmp(info.BldNumStr, bench[i][2])) {
        std::printf(errMsgFormat, bench[i][2], info.BldNumStr);
        CHECK(false);
      }
    }
  }
}
