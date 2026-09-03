/** @file

   Catch-based tests for RecLookupMatchingRecords regex handling.

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

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

#include "records/RecCore.h"
#include "tsutil/Metrics.h"
#include "test_Diags.h"

namespace
{
void
count_callback(const RecRecord * /* record ATS_UNUSED */, void *data)
{
  ++*static_cast<int *>(data);
}

} // namespace

// Bounded and unbounded both end in "no match" here, so only elapsed time separates them. At
// PCRE2's 10,000,000 default a name costs about 20ms, so RECORD_COUNT is sized to put the unbounded
// path several times past the threshold while the bounded one stays near 6ms. Lower it and the
// unbounded path also finishes in time, leaving the test guarding nothing.
TEST_CASE("RecLookupMatchingRecords bounds regex backtracking", "[librecords][RecCore][match-limit]")
{
  constexpr int  RECORD_COUNT   = 300;
  constexpr auto TIME_THRESHOLD = std::chrono::seconds{2};

  // A long 'a' run followed by a character that cannot satisfy the anchor is what drives "(a+)+$"
  // into exponential backtracking.
  const std::string suffix = std::string(40, 'a') + "z";

  for (int i = 0; i < RECORD_COUNT; ++i) {
    const std::string name = "proxy.config.test.matchlimit." + std::to_string(i) + "." + suffix;
    REQUIRE(RecRegisterConfigInt(RECT_CONFIG, name.c_str(), 0, RECU_DYNAMIC, RECC_NULL, nullptr, REC_SOURCE_NULL) == REC_ERR_OKAY);
  }

  CatchDiags *cdiag = static_cast<CatchDiags *>(diags());
  cdiag->messages.clear();

  int  matches = 0;
  auto start   = std::chrono::steady_clock::now();
  REQUIRE(RecLookupMatchingRecords(RECT_CONFIG, "(a+)+$", count_callback, &matches) == REC_ERR_OKAY);
  auto elapsed = std::chrono::steady_clock::now() - start;

  CHECK(matches == 0);
  CHECK(elapsed < TIME_THRESHOLD);

  // Unlike the timing check above, this holds regardless of machine speed, so it is what actually
  // pins the reporting behaviour.
  REQUIRE(cdiag->messages.size() == 1);
  CHECK(cdiag->messages[0].find("results are incomplete") != std::string::npos);
}

// Metrics are two more exec() sites, reached only for RECT_PROCESS, RECT_NODE and RECT_PLUGIN. The
// RECT_CONFIG case above walks the record table instead, so it never enters them.
TEST_CASE("RecLookupMatchingRecords bounds regex backtracking for metrics", "[librecords][RecCore][match-limit]")
{
  const std::string suffix = std::string(40, 'a') + "z";

  ts::Metrics::Counter::createPtr("proxy.process.test.matchlimit.counter." + suffix);
  ts::Metrics::StaticString::createString("proxy.process.test.matchlimit.string." + suffix, "value");

  CatchDiags *cdiag = static_cast<CatchDiags *>(diags());
  cdiag->messages.clear();

  int matches = 0;

  REQUIRE(RecLookupMatchingRecords(RECT_PROCESS, "(a+)+$", count_callback, &matches) == REC_ERR_OKAY);

  // Only the two crafted names can exhaust the limit, so the count is what shows both loops ran
  // rather than just one.
  REQUIRE(cdiag->messages.size() == 1);
  CHECK(cdiag->messages[0].find("on 2 name(s)") != std::string::npos);
}

// The fourth exec() site, reached only when RECT_HIDDEN_METRIC is requested, since hidden metrics
// are excluded even from RECT_ALL, see RecDefs.h. No case above sets that bit.
TEST_CASE("RecLookupMatchingRecords bounds regex backtracking for hidden metrics", "[librecords][RecCore][match-limit]")
{
  const std::string suffix = std::string(40, 'a') + "z";
  auto             *m      = ts::Metrics::Gauge::createHiddenPtr("proxy.process.test.matchlimit.hidden." + suffix);

  REQUIRE(m != nullptr);

  CatchDiags *cdiag = static_cast<CatchDiags *>(diags());
  cdiag->messages.clear();

  int matches = 0;

  REQUIRE(RecLookupMatchingRecords(RECT_HIDDEN_METRIC, "(a+)+$", count_callback, &matches) == REC_ERR_OKAY);

  // The hidden store holds only what this file registers, so the crafted name is the only entry
  // able to exhaust the limit.
  REQUIRE(cdiag->messages.size() == 1);
  CHECK(cdiag->messages[0].find("on 1 name(s)") != std::string::npos);
}

// The limit must also be loose enough for real lookups, or it silently drops records. Two
// sequential ".*" proving no match is the most expensive shape a legitimate caller is likely to
// write, at about L^2/2 steps for length L.
//
// The 250 character name below, far longer than the 66 character longest in a stock build, needs
// about 25,400 steps: half of the 50,000 limit, so it is insensitive to step-accounting differences
// between PCRE2 versions, yet 2.5x above 10,000, so reverting the limit to that or to 1750 fails
// here rather than quietly truncating in production.
TEST_CASE("RecLookupMatchingRecords does not truncate legitimate lookups", "[librecords][RecCore]")
{
  const std::string prefix = "proxy.config.test.longname.";
  const std::string name   = prefix + std::string(250 - prefix.size(), 'x');

  REQUIRE(RecRegisterConfigInt(RECT_CONFIG, name.c_str(), 0, RECU_DYNAMIC, RECC_NULL, nullptr, REC_SOURCE_NULL) == REC_ERR_OKAY);

  CatchDiags *cdiag = static_cast<CatchDiags *>(diags());
  cdiag->messages.clear();

  // Anchoring on the prefix keeps every other registered name to a couple of steps, so this
  // measures the long name and nothing else.
  int matches = 0;

  REQUIRE(RecLookupMatchingRecords(RECT_CONFIG, "^proxy\\.config\\.test\\.longname\\..*.*[~=]", count_callback, &matches) ==
          REC_ERR_OKAY);

  CHECK(matches == 0);
  CHECK(cdiag->messages.empty());
}

TEST_CASE("RecLookupMatchingRecords still matches ordinary patterns", "[librecords][RecCore]")
{
  REQUIRE(RecRegisterConfigInt(RECT_CONFIG, "proxy.config.test.lookup.plain", 7, RECU_DYNAMIC, RECC_NULL, nullptr,
                               REC_SOURCE_NULL) == REC_ERR_OKAY);

  int matches = 0;
  REQUIRE(RecLookupMatchingRecords(RECT_CONFIG, "proxy\\.config\\.test\\.lookup\\.plain", count_callback, &matches) ==
          REC_ERR_OKAY);
  CHECK(matches == 1);
}
