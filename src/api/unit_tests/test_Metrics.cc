/** @file

    TextView unit tests.

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

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

#include "api/Metrics.h"

TEST_CASE("Metrics", "[libtsapi][Metrics]")
{
  ts::Metrics m;

  SECTION("iterator")
  {
    auto [name, value] = *m.begin();
    REQUIRE(value == 0);
    REQUIRE(name == "proxy.process.api.metrics.bad_id");

    REQUIRE(m.begin() != m.end());
    REQUIRE(++m.begin() == m.end());

    auto it = m.begin();
    it++;
    REQUIRE(it == m.end());
  }

  SECTION("New metric")
  {
    auto fooid = m.newMetric("foo");

    REQUIRE(fooid == 1);
    REQUIRE(m.name(fooid) == "foo");

    REQUIRE(m[fooid].load() == 0);
    m.increment(fooid);
    REQUIRE(m[fooid].load() == 1);
  }

  SECTION("operator[]")
  {
    m[0].store(42);

    REQUIRE(m[0].load() == 42);
  }

  SECTION("Span allocation")
  {
    ts::Metrics::IdType span_id;
    auto fooid = m.newMetric("foo"); // To see that span_id gets to 2
    auto span  = m.newMetricSpan(17, &span_id);

    REQUIRE(span.size() == 17);
    REQUIRE(fooid == 1);
    REQUIRE(span_id == 2);

    m.rename(span_id + 0, "span.0");
    m.rename(span_id + 1, "span.1");
    m.rename(span_id + 2, "span.2");
    REQUIRE(m.name(fooid) == "foo");
    REQUIRE(m.name(span_id + 0) == "span.0");
    REQUIRE(m.name(span_id + 1) == "span.1");
    REQUIRE(m.name(span_id + 2) == "span.2");
    m.rename(fooid, "foo-new");
    REQUIRE(m.name(fooid) == "foo-new");
    REQUIRE(m.lookup("foo") == ts::Metrics::NOT_FOUND);
    REQUIRE(m.lookup("foo-new") == fooid);
  }

  SECTION("lookup")
  {
    auto nm = m.lookupPtr("notametric");
    REQUIRE(!nm);

    auto mid = m.newMetric("ametric");
    auto fm  = m.lookupPtr("ametric");
    REQUIRE(fm.has_value());
    REQUIRE(fm.value());
    REQUIRE(fm.value() == m.lookup(mid));
    REQUIRE(m.lookup("ametric") == mid);
  }
}
