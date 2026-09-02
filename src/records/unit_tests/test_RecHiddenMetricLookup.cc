/** @file

   Catch-based tests for hidden metric lookup through librecords

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
#include <string>
#include <vector>

#include "../P_RecCore.h"
#include "tsutil/Metrics.h"

namespace
{
struct LookupEntry {
  RecT        rec_type;
  std::string name;
  RecInt      int_value{0};
};

void
collect(const RecRecord *record, void *data)
{
  auto *entries = static_cast<std::vector<LookupEntry> *>(data);

  entries->push_back({record->rec_type, record->name ? record->name : "", record->data.rec_int});
}

// Mirrors the check the JSONRPC record lookup applies to every record it is handed, see
// rpc::handlers::utils::get_yaml_record_regex(). A record whose rec_type shares no bit with
// the requested mask is rejected with REQUESTED_TYPE_MISMATCH.
bool
passes_requested_type_check(unsigned requested, RecT rec_type)
{
  return (requested & rec_type) != 0;
}

} // namespace

TEST_CASE("RecLookupMatchingRecords - hidden metrics", "[librecords][RecLookup][hidden]")
{
  const std::string name = "proxy.test.lookup.hidden_gauge";
  auto             *m    = ts::Metrics::Gauge::createHiddenPtr(name);

  REQUIRE(m != nullptr);
  m->store(42);

  SECTION("a hidden-only request returns the metric and survives the requested-type check")
  {
    std::vector<LookupEntry> entries;

    REQUIRE(RecLookupMatchingRecords(RECT_HIDDEN_METRIC, name.c_str(), collect, &entries) == REC_ERR_OKAY);

    bool found = false;

    for (const auto &e : entries) {
      if (e.name == name) {
        found = true;
        CHECK(e.int_value == 42);
        // Both bits must be set: RECT_HIDDEN_METRIC so a caller that asked only for hidden
        // metrics is not rejected, and RECT_PROCESS so the record still encodes as a metric.
        CHECK((e.rec_type & RECT_HIDDEN_METRIC) != 0);
        CHECK((e.rec_type & RECT_PROCESS) != 0);
        CHECK(passes_requested_type_check(RECT_HIDDEN_METRIC, e.rec_type));
        break;
      }
    }

    REQUIRE(found);
  }

  SECTION("an include-hidden request also survives the requested-type check")
  {
    std::vector<LookupEntry> entries;
    const unsigned           requested = RECT_PROCESS | RECT_NODE | RECT_PLUGIN | RECT_HIDDEN_METRIC;

    REQUIRE(RecLookupMatchingRecords(requested, name.c_str(), collect, &entries) == REC_ERR_OKAY);

    bool found = false;

    for (const auto &e : entries) {
      if (e.name == name) {
        found = true;
        CHECK(passes_requested_type_check(requested, e.rec_type));
        break;
      }
    }

    REQUIRE(found);
  }

  SECTION("hidden metrics stay out of a normal RECT_ALL request")
  {
    std::vector<LookupEntry> entries;

    REQUIRE(RecLookupMatchingRecords(RECT_ALL, name.c_str(), collect, &entries) == REC_ERR_OKAY);

    for (const auto &e : entries) {
      CHECK(e.name != name);
    }
  }
}

TEST_CASE("RecLookupMatchingRecords - unlisted metrics", "[librecords][RecLookup][unlisted]")
{
  const std::string name = "proxy.test.lookup.unlisted_gauge";
  auto             *m    = ts::Metrics::Gauge::createPtr(name);

  REQUIRE(m != nullptr);
  m->store(7);

  auto &metrics = ts::Metrics::instance();
  auto  id      = metrics.lookup(name);

  REQUIRE(id != ts::Metrics::NOT_FOUND);
  REQUIRE(metrics.unlist(id));

  SECTION("an unlisted metric is not enumerated")
  {
    std::vector<LookupEntry> entries;

    REQUIRE(RecLookupMatchingRecords(RECT_ALL, name.c_str(), collect, &entries) == REC_ERR_OKAY);

    for (const auto &e : entries) {
      CHECK(e.name != name);
    }
  }

  SECTION("an unlisted metric is still found by exact name")
  {
    // RecLookupRecord resolves through Metrics::lookup() rather than iteration, which is what keeps
    // logging fields and TSStatFindName working across an unlisting.
    std::vector<LookupEntry> entries;

    REQUIRE(RecLookupRecord(name.c_str(), collect, &entries) == REC_ERR_OKAY);
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].name == name);
    CHECK(entries[0].int_value == 7);
  }

  SECTION("relisting puts it back in enumeration")
  {
    REQUIRE(metrics.relist(id));

    std::vector<LookupEntry> entries;
    bool                     found = false;

    REQUIRE(RecLookupMatchingRecords(RECT_ALL, name.c_str(), collect, &entries) == REC_ERR_OKAY);
    for (const auto &e : entries) {
      if (e.name == name) {
        found = true;
        CHECK(e.int_value == 7);
      }
    }

    REQUIRE(found);
  }
}
