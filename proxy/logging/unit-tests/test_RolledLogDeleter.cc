/** @file

  Catch-based tests for RolledLogDeleter.

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

#include <RolledLogDeleter.h>

#include "tscore/ts_file.h"

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

namespace fs = ts::file;

const fs::path log_dir("/home/y/logs/trafficserver");

void
verify_there_are_no_candidates(RolledLogDeleter &deleter)
{
  CHECK_FALSE(deleter.has_candidates());
  CHECK(deleter.get_candidate_count() == 0);
}

void
verify_rolled_log_behavior(RolledLogDeleter &deleter, fs::path rolled_log1, fs::path rolled_log2, fs::path rolled_log3)
{
  SECTION("Verify we can add a single rolled file")
  {
    constexpr int64_t file_size    = 100;
    constexpr time_t last_modified = 30;

    REQUIRE(deleter.consider_for_candidacy(rolled_log1.string(), file_size, last_modified));

    CHECK(deleter.has_candidates());
    CHECK(deleter.get_candidate_count() == 1);

    const auto next_candidate = deleter.take_next_candidate_to_delete();
    CHECK(next_candidate->rolled_log_path == rolled_log1.string());

    // Everything has been taken.
    verify_there_are_no_candidates(deleter);
  }

  SECTION("Verify we can add two rolled log files")
  {
    constexpr int64_t file_size         = 100;
    constexpr time_t oldest_timestamp   = 30;
    constexpr time_t youngest_timestamp = 60;

    // Intentionally insert them out of order (that is, the first one to delete
    // is the second added).
    REQUIRE(deleter.consider_for_candidacy(rolled_log2.string(), file_size, youngest_timestamp));
    REQUIRE(deleter.consider_for_candidacy(rolled_log1.string(), file_size, oldest_timestamp));

    CHECK(deleter.has_candidates());
    CHECK(deleter.get_candidate_count() == 2);

    // The first candidate should be the oldest modified one.
    auto next_candidate = deleter.take_next_candidate_to_delete();
    CHECK(next_candidate->rolled_log_path == rolled_log1.string());

    CHECK(deleter.has_candidates());
    CHECK(deleter.get_candidate_count() == 1);

    // The second candidate should be the remaining one.
    next_candidate = deleter.take_next_candidate_to_delete();
    CHECK(next_candidate->rolled_log_path == rolled_log2.string());

    // Everything has been taken.
    verify_there_are_no_candidates(deleter);
  }

  SECTION("Verify we can add three rolled log files")
  {
    constexpr int64_t file_size = 100;

    constexpr time_t oldest_timestamp   = 30;
    constexpr time_t youngest_timestamp = 60;
    constexpr time_t middle_timestamp   = 45;

    // Intentionally insert them out of order.
    REQUIRE(deleter.consider_for_candidacy(rolled_log2.string(), file_size, youngest_timestamp));
    REQUIRE(deleter.consider_for_candidacy(rolled_log1.string(), file_size, oldest_timestamp));
    REQUIRE(deleter.consider_for_candidacy(rolled_log3.string(), file_size, middle_timestamp));

    CHECK(deleter.has_candidates());
    CHECK(deleter.get_candidate_count() == 3);

    // The first candidate should be the oldest modified one.
    auto next_candidate = deleter.take_next_candidate_to_delete();
    CHECK(next_candidate->rolled_log_path == rolled_log1.string());

    CHECK(deleter.has_candidates());
    CHECK(deleter.get_candidate_count() == 2);

    // The second candidate should be the second oldest.
    next_candidate = deleter.take_next_candidate_to_delete();
    CHECK(next_candidate->rolled_log_path == rolled_log3.string());

    CHECK(deleter.has_candidates());
    CHECK(deleter.get_candidate_count() == 1);

    // The third candidate should be the remaining one.
    next_candidate = deleter.take_next_candidate_to_delete();
    CHECK(next_candidate->rolled_log_path == rolled_log2.string());

    // Everything has been taken.
    verify_there_are_no_candidates(deleter);
  }
}

TEST_CASE("Rotated diags logs can be added and removed", "[RolledLogDeleter]")
{
  RolledLogDeleter deleter;
  constexpr auto min_count = 0;
  deleter.register_log_type_for_deletion("diags.log", min_count);

  const fs::path rolled_log1 = log_dir / "diags.log.20191117.16h43m15s-20191118.16h43m15s.old";
  const fs::path rolled_log2 = log_dir / "diags.log.20191118.16h43m15s-20191122.04h07m09s.old";
  const fs::path rolled_log3 = log_dir / "diags.log.20191122.04h07m09s-20191124.00h12m47s.old";

  verify_there_are_no_candidates(deleter);
  verify_rolled_log_behavior(deleter, rolled_log1, rolled_log2, rolled_log3);
}

TEST_CASE("Rotated squid logs can be added and removed", "[RolledLogDeleter]")
{
  RolledLogDeleter deleter;
  constexpr auto min_count = 0;
  deleter.register_log_type_for_deletion("squid.log", min_count);
  const fs::path rolled_log1 = log_dir / "squid.log_some.hostname.com.20191125.19h00m04s-20191125.19h15m04s.old";
  const fs::path rolled_log2 = log_dir / "squid.log_some.hostname.com.20191125.19h15m04s-20191125.19h30m04s.old";
  const fs::path rolled_log3 = log_dir / "squid.log_some.hostname.com.20191125.19h30m04s-20191125.19h45m04s.old";

  verify_there_are_no_candidates(deleter);
  verify_rolled_log_behavior(deleter, rolled_log1, rolled_log2, rolled_log3);
}

TEST_CASE("clear removes all candidates", "[RolledLogDeleter]")
{
  RolledLogDeleter deleter;
  constexpr auto min_count = 0;
  deleter.register_log_type_for_deletion("squid.log", min_count);
  deleter.register_log_type_for_deletion("diags.log", min_count);

  constexpr auto size         = 10;
  constexpr time_t time_stamp = 20;

  // Add some candidates.
  REQUIRE(deleter.consider_for_candidacy("squid.log_arbitrary-text-1", size, time_stamp));
  REQUIRE(deleter.consider_for_candidacy("squid.log_arbitrary-text-2", size, time_stamp));
  REQUIRE(deleter.consider_for_candidacy("squid.log_arbitrary-text-3", size, time_stamp));

  REQUIRE(deleter.consider_for_candidacy("diags.log.arbitrary-text-1", size, time_stamp));
  REQUIRE(deleter.consider_for_candidacy("diags.log.arbitrary-text-2", size, time_stamp));
  REQUIRE(deleter.consider_for_candidacy("diags.log.arbitrary-text-3", size, time_stamp));

  REQUIRE(deleter.has_candidates());
  REQUIRE(deleter.get_candidate_count() == 6);

  deleter.clear_candidates();
  verify_there_are_no_candidates(deleter);
}

TEST_CASE("verify priority enforcement", "[RolledLogDeleter]")
{
  RolledLogDeleter deleter;

  constexpr auto low_min_count     = 1;
  constexpr auto medium_min_count  = 3;
  constexpr auto highest_min_count = 0;

  constexpr int64_t a_size = 10;
  constexpr time_t a_time  = 30;

  deleter.register_log_type_for_deletion("squid.log", low_min_count);
  deleter.register_log_type_for_deletion("traffic.out", medium_min_count);
  deleter.register_log_type_for_deletion("diags.log", highest_min_count);

  /* The previous tests verify selection within a log_type which is done based
   * upon last modified time stamp. These tests focus on selection of
   * candidates across log types, which is based upon number of candidates and
   * the desired min_count. */
  SECTION("Verify selection of a candidate when there is only one.")
  {
    const fs::path rolled_squid = log_dir / "squid.log_some.hostname.com.20191125.19h00m04s-20191125.19h15m04s.old";
    REQUIRE(deleter.consider_for_candidacy(rolled_squid.view(), a_size, a_time));
    const auto next_candidate = deleter.take_next_candidate_to_delete();
    CHECK(next_candidate->rolled_log_path == rolled_squid.string());
    verify_there_are_no_candidates(deleter);
  }

  SECTION("Verify selection of candidates across three types.")
  {
    const fs::path rolled_squid   = log_dir / "squid.log_some.hostname.com.20191125.19h00m04s-20191125.19h15m04s.old";
    const fs::path rolled_traffic = log_dir / "traffic.out.20191118.16h43m11s-20191122.01h30m30s.old";
    const fs::path rolled_diags   = log_dir / "diags.log.20191117.16h43m15s-20191118.16h43m15s.old";

    REQUIRE(deleter.consider_for_candidacy(rolled_squid.view(), a_size, a_time));
    REQUIRE(deleter.consider_for_candidacy(rolled_traffic.view(), a_size, a_time));
    REQUIRE(deleter.consider_for_candidacy(rolled_diags.view(), a_size, a_time));

    // Since the time stamps of both are the same, selection should be made
    // based upon min_count.
    auto next_candidate = deleter.take_next_candidate_to_delete();
    CHECK(next_candidate->rolled_log_path == rolled_squid.string());

    next_candidate = deleter.take_next_candidate_to_delete();
    CHECK(next_candidate->rolled_log_path == rolled_traffic.string());

    next_candidate = deleter.take_next_candidate_to_delete();
    CHECK(next_candidate->rolled_log_path == rolled_diags.string());

    verify_there_are_no_candidates(deleter);
  }

  SECTION("Verify that number of candidates is taken into account.")
  {
    const fs::path rolled_squid    = log_dir / "squid.log_some.hostname.com.20191125.19h00m04s-20191125.19h15m04s.old";
    const fs::path rolled_traffic1 = log_dir / "traffic.out.20191117.16h43m15s-20191118.16h43m15s.old";
    const fs::path rolled_traffic2 = log_dir / "traffic.out.20191118.16h43m15s-20191122.04h07m09s.old";
    const fs::path rolled_traffic3 = log_dir / "traffic.out.20191122.04h07m09s-20191124.00h12m47s.old";
    const fs::path rolled_traffic4 = log_dir / "traffic.out.20191124.00h12m44s-20191125.00h12m44s.old";

    constexpr time_t old       = 60;
    constexpr time_t older     = 30;
    constexpr time_t oldest    = 10;
    constexpr time_t oldestest = 5;

    REQUIRE(deleter.consider_for_candidacy(rolled_squid.view(), a_size, a_time));
    REQUIRE(deleter.consider_for_candidacy(rolled_traffic1.view(), a_size, old));
    REQUIRE(deleter.consider_for_candidacy(rolled_traffic2.view(), a_size, older));
    REQUIRE(deleter.consider_for_candidacy(rolled_traffic3.view(), a_size, oldest));
    REQUIRE(deleter.consider_for_candidacy(rolled_traffic4.view(), a_size, oldestest));

    // The user has requested a higher number of traffic.out files, but since
    // there are so many of them, the oldest of them should be selected next.
    auto next_candidate = deleter.take_next_candidate_to_delete();
    CHECK(next_candidate->rolled_log_path == rolled_traffic4.string());

    // Next, squid.log should be chosen.
    next_candidate = deleter.take_next_candidate_to_delete();
    CHECK(next_candidate->rolled_log_path == rolled_squid.string());

    // Now, there's only traffic.out files.
    next_candidate = deleter.take_next_candidate_to_delete();
    CHECK(next_candidate->rolled_log_path == rolled_traffic3.string());
    next_candidate = deleter.take_next_candidate_to_delete();
    CHECK(next_candidate->rolled_log_path == rolled_traffic2.string());
    next_candidate = deleter.take_next_candidate_to_delete();
    CHECK(next_candidate->rolled_log_path == rolled_traffic1.string());

    verify_there_are_no_candidates(deleter);
  }

  SECTION("A mincount of 0 should shield from deletion as much as possible")
  {
    const fs::path rolled_traffic = log_dir / "traffic.out.20191117.16h43m15s-20191118.16h43m15s.old";
    const fs::path rolled_diags1  = log_dir / "diags.log.20191117.16h43m15s-20191118.16h43m15s.old";
    const fs::path rolled_diags2  = log_dir / "diags.log.20191118.16h43m15s-20191122.04h07m09s.old";
    const fs::path rolled_diags3  = log_dir / "diags.log.20191122.04h07m09s-20191124.00h12m47s.old";
    const fs::path rolled_diags4  = log_dir / "diags.log.20191124.00h12m44s-20191125.00h12m44s.old";

    constexpr time_t old       = 60;
    constexpr time_t older     = 30;
    constexpr time_t oldest    = 10;
    constexpr time_t oldestest = 5;

    REQUIRE(deleter.consider_for_candidacy(rolled_traffic.view(), a_size, a_time));
    REQUIRE(deleter.consider_for_candidacy(rolled_diags1.view(), a_size, old));
    REQUIRE(deleter.consider_for_candidacy(rolled_diags2.view(), a_size, older));
    REQUIRE(deleter.consider_for_candidacy(rolled_diags3.view(), a_size, oldest));
    REQUIRE(deleter.consider_for_candidacy(rolled_diags4.view(), a_size, oldestest));

    // Even with so many diags.log files, the traffic.out one should be
    // selected first because the min_count of diags.log is 0.
    auto next_candidate = deleter.take_next_candidate_to_delete();
    CHECK(next_candidate->rolled_log_path == rolled_traffic.string());

    // Now there's only diags.log files.
    next_candidate = deleter.take_next_candidate_to_delete();
    CHECK(next_candidate->rolled_log_path == rolled_diags4.string());
    next_candidate = deleter.take_next_candidate_to_delete();
    CHECK(next_candidate->rolled_log_path == rolled_diags3.string());
    next_candidate = deleter.take_next_candidate_to_delete();
    CHECK(next_candidate->rolled_log_path == rolled_diags2.string());
    next_candidate = deleter.take_next_candidate_to_delete();
    CHECK(next_candidate->rolled_log_path == rolled_diags1.string());

    verify_there_are_no_candidates(deleter);
  }
}

//
// Stub
//
void
RecSignalManager(int, const char *, unsigned long)
{
  ink_release_assert(false);
}
