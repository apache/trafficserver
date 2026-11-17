/** @file

  Unit tests for the plugin log address marshaling API.

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

#include "ts/ts.h"
#include "proxy/logging/LogField.h"
#include "tscore/ink_inet.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

TEST_CASE("TSLogAddrMarshal terminates a full unix socket path", "[api][log]")
{
  // A kernel sockaddr_un may carry a full, unterminated sun_path, so none of the guard bytes after it may reach the output.
  struct {
    sockaddr_un sun;
    char        guard[16];
  } raw;
  memset(&raw, 'g', sizeof(raw));
  raw.guard[sizeof(raw.guard) - 1] = '\0';
  raw.sun.sun_family               = AF_UNIX;
  memset(raw.sun.sun_path, 'z', TS_UNIX_SIZE);
  auto             *sa = reinterpret_cast<sockaddr *>(&raw.sun);
  std::string const truncated(TS_UNIX_SIZE - 1, 'z');

  std::vector<char> marshalled(TSLogAddrMarshal(nullptr, sa));
  REQUIRE(TSLogAddrMarshal(marshalled.data(), sa) == static_cast<int>(marshalled.size()));

  auto const *un = reinterpret_cast<LogFieldUn const *>(marshalled.data());
  CHECK(std::string_view(un->_path, strnlen(un->_path, TS_UNIX_SIZE)) == truncated);

  char  dest[256];
  char *src                 = marshalled.data();
  auto [read_len, text_len] = TSLogAddrUnmarshal(&src, dest, sizeof(dest));
  CHECK(read_len == static_cast<int>(marshalled.size()));
  REQUIRE(text_len >= 0);
  CHECK(std::string_view(dest, text_len) == truncated);
}
