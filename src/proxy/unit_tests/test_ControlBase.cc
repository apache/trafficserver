/** @file

  Unit tests for ControlBase.

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

#include <string>
#include <string_view>

#include "proxy/ControlBase.h"
#include "proxy/ControlMatcher.h"
#include "proxy/hdrs/HTTP.h"
#include "proxy/hdrs/MIME.h"
#include "proxy/hdrs/URL.h"
#include "tscore/MatcherUtils.h"

using namespace std::literals;

extern int cmd_disable_pfreelist;

namespace
{
void
initialize_headers_once()
{
  static bool initialized = false;
  if (!initialized) {
    cmd_disable_pfreelist = true;
    url_init();
    mime_init();
    http_init();
    initialized = true;
  }
}

// Apply "method=<config_method>" to a ControlBase and check it against a
// request whose method is <request_method>.
bool
method_matches(std::string_view config_method, std::string_view request_method)
{
  initialize_headers_once();

  HTTPHdr hdr;
  hdr.create(HTTPType::REQUEST);
  hdr.method_set(request_method);

  HttpRequestData req;
  req.hdr = &hdr;

  std::string  label{"method"};
  std::string  value{config_method};
  matcher_line line{};
  line.num_el     = 1;
  line.line[0][0] = label.data();
  line.line[1][0] = value.data();

  ControlBase cb;
  REQUIRE(cb.ProcessModifiers(&line) == nullptr);
  bool matched = cb.CheckModifiers(&req);

  hdr.destroy();
  return matched;
}
} // namespace

TEST_CASE("ControlBase MethodMod check", "[ControlBase]")
{
  // Exact match (case-insensitive).
  CHECK(method_matches("GET", "GET"));
  CHECK(method_matches("GET", "get"));

  // Different method.
  CHECK_FALSE(method_matches("GET", "POST"));

  // Make sure it's not a prefix match
  CHECK_FALSE(method_matches("GET", "GETT"));
  CHECK_FALSE(method_matches("GET", "GETS"));
  CHECK_FALSE(method_matches("POST", "POSTING"));
  CHECK_FALSE(method_matches("PUT", "PUTS"));
}
