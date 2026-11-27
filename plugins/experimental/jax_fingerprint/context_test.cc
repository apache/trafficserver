/** @file

  Tests for the client address a JAx context records.

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

#include "plugin.h"
#include "context.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <string_view>

DbgCtl dbg_ctl{PLUGIN_NAME};

TEST_CASE("JAx context records a unix socket path", "[jax_fingerprint][context]")
{
  // A kernel sockaddr_un may fill sun_path with no terminator, so none of the guard bytes after it may reach the address.
  struct {
    sockaddr_un sun;
    char        guard[16];
  } raw;

  memset(&raw, 'g', sizeof(raw));
  raw.guard[sizeof(raw.guard) - 1] = '\0';
  raw.sun.sun_family               = AF_UNIX;

  SECTION("a path that fills sun_path")
  {
    memset(raw.sun.sun_path, 'z', sizeof(raw.sun.sun_path));
    JAxContext ctx("test", reinterpret_cast<sockaddr const *>(&raw.sun));
    CHECK(std::string_view(ctx.get_addr()) == std::string(sizeof(raw.sun.sun_path), 'z'));
  }

  SECTION("a terminated path")
  {
    strcpy(raw.sun.sun_path, "/run/ats/client.sock");
    JAxContext ctx("test", reinterpret_cast<sockaddr const *>(&raw.sun));
    CHECK(std::string_view(ctx.get_addr()) == "/run/ats/client.sock");
  }
}
