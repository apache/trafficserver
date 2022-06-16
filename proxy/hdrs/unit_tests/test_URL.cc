/** @file

   Catch-based unit tests for URL

   @section license License

   Licensed to the Apache Software Foundation (ASF) under one or more contributor license agreements.
   See the NOTICE file distributed with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance with the License.  You may obtain a
   copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied. See the License for the specific language governing permissions and limitations under
   the License.
 */

#include <cstdio>

#include "catch.hpp"

#include "URL.h"

TEST_CASE("Validate Scheme", "[proxy][validscheme]")
{
  static const struct {
    std::string_view text;
    bool valid;
  } scheme_test_cases[] = {{"http", true},      {"https", true},      {"example", true},    {"example.", true},
                           {"example++", true}, {"example--.", true}, {"++example", false}, {"--example", false},
                           {".example", false}, {"example://", false}};

  for (auto i : scheme_test_cases) {
    // it's pretty hard to debug with
    //     CHECK(validate_scheme(i.text) == i.valid);

    std::string_view text = i.text;
    if (validate_scheme(text) != i.valid) {
      std::printf("Validation of scheme: \"%s\", expected %s, but not\n", text.data(), (i.valid ? "true" : "false"));
      CHECK(false);
    }
  }
}
