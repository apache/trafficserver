/** @file

    Errata unit tests.

    @section license License

    Licensed to the Apache Software Foundation (ASF) under one or more contributor license
    agreements.  See the NOTICE file distributed with this work for additional information regarding
    copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
    (the "License"); you may not use this file except in compliance with the License.  You may
    obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software distributed under the
    License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
    express or implied. See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "swoc/Errata.h"
#include "swoc/ext/catch.hpp"

using swoc::Errata;
using namespace std::literals;

Errata
Noteworthy(std::string_view text)
{
  Errata notes;
  notes.info(text);
  return notes;
}

Errata
cycle(Errata &erratum)
{
  erratum.info("Note well, young one!");
  return erratum;
}

TEST_CASE("Errata copy", "[libswoc][Errata]")
{
  auto notes = Noteworthy("Evil Dave Rulz.");
  REQUIRE(notes.count() == 1);
  REQUIRE(notes.begin()->text() == "Evil Dave Rulz.");

  notes = cycle(notes);
  REQUIRE(notes.count() == 2);

  Errata erratum;
  erratum.clear();
  REQUIRE(erratum.count() == 0);
  erratum.diag("Diagnostics");
  REQUIRE(erratum.count() == 1);
  erratum.info("Information");
  REQUIRE(erratum.count() == 2);
  erratum.warn("Warning");
  REQUIRE(erratum.count() == 3);
  erratum.error("Error");
  REQUIRE(erratum.count() == 4);

  // Test internal allocation boundaries.
  notes.clear();
  std::string_view text{"0123456789012345678901234567890123456789"};
  for (int i = 0; i < 50; ++i) {
    notes.info(text);
  }
  REQUIRE(notes.count() == 50);
  REQUIRE(notes.begin()->text() == text);
  bool match_p = true;
  for (auto &&note : notes) {
    if (note.text() != text) {
      match_p = false;
      break;
    }
  }
  REQUIRE(match_p);
};
