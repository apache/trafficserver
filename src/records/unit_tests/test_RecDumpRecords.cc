/** @file

   Catch-based tests for RecDumpRecords

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

struct DumpEntry {
  RecT        rec_type;
  int         registered;
  std::string name;
  int         data_type;
  std::string string_value;
  RecInt      int_value{0};
};

static void
collect_callback(RecT rec_type, void *edata, int registered, const char *name, int data_type, RecData *datum)
{
  auto     *entries = static_cast<std::vector<DumpEntry> *>(edata);
  DumpEntry entry;

  entry.rec_type   = rec_type;
  entry.registered = registered;
  entry.name       = name;
  entry.data_type  = data_type;

  if (data_type == RECD_STRING && datum->rec_string != nullptr) {
    entry.string_value = datum->rec_string;
  } else if (data_type == RECD_INT || data_type == RECD_COUNTER) {
    entry.int_value = datum->rec_int;
  }

  entries->push_back(std::move(entry));
}

TEST_CASE("RecDumpRecords - StaticString metrics", "[librecords][RecDump]")
{
  const std::string test_name  = "proxy.test.dump.string_metric";
  const std::string test_value = "test_string_value";

  ts::Metrics::StaticString::createString(test_name, test_value);

  std::vector<DumpEntry> entries;

  RecDumpRecords(RECT_NULL, collect_callback, &entries);

  bool found = false;

  for (const auto &entry : entries) {
    if (entry.name == test_name) {
      found = true;
      CHECK(entry.rec_type == RECT_PLUGIN);
      CHECK(entry.registered == 1);
      CHECK(entry.data_type == RECD_STRING);
      CHECK(entry.string_value == test_value);
      break;
    }
  }

  REQUIRE(found);
}
