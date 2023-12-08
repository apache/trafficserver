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
using ts::Metrics;

TEST_CASE("Metrics", "[libtsapi][Metrics]")
{
  auto &m = Metrics::instance();

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
    auto fooid = Metrics::Counter::create("foo");

    REQUIRE(fooid == 1);
    REQUIRE(m.name(fooid) == "foo");

    REQUIRE(m[fooid].load() == 0);
    m.increment(fooid);
    REQUIRE(m[fooid].load() == 1);
  }

  SECTION("operator[] & store")
  {
    auto storeid = Metrics::Gauge::create("store");

    m[storeid].store(42);

    REQUIRE(m[storeid].load() == 42);
  }

  SECTION("Span allocation")
  {
    ts::Metrics::IdType span_id;
    auto fooid = m.lookup("foo");
    auto span  = Metrics::Counter::createSpan(17, &span_id);

    REQUIRE(span.size() == 17);
    REQUIRE(fooid == 1);
    REQUIRE(span_id == 3);

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
    auto nm = m.lookup("notametric");
    REQUIRE(nm == ts::Metrics::NOT_FOUND);

    auto mid  = Metrics::Counter::create("ametric");
    auto fmid = m.lookup("ametric");

    REQUIRE(mid == fmid);
  }

  SECTION("histogram")
  {
    using Histogram = Metrics::Histogram<7, 2>;
    auto h          = Histogram();

    h(12);
    REQUIRE(h[10] == 1);

    for (Histogram::raw_type i = 0; i < Histogram::N_BUCKETS; i++) {
      printf("bucket %lu = %lu\n", i, h[i]);
    }

    REQUIRE(h.min_for_bucket(0) == 0);
    REQUIRE(h.min_for_bucket(3) == 3);
    REQUIRE(h.min_for_bucket(4) == 4);
    REQUIRE(h.min_for_bucket(8) == 8);
    REQUIRE(h.min_for_bucket(9) == 10);
    REQUIRE(h.min_for_bucket(12) == 16);
    REQUIRE(h.min_for_bucket(13) == 20);
    REQUIRE(h.min_for_bucket(16) == 32);
    REQUIRE(h.min_for_bucket(17) == 40);

    for (auto x : {0, 1, 4, 6, 19, 27, 36, 409, 16000, 1097}) {
      h(x);
    }
    REQUIRE(h[0] == 1);
    REQUIRE(h[1] == 1);
    REQUIRE(h[2] == 0);
    REQUIRE(h[12] == 1); // sample 19 should be here.
    REQUIRE(h[14] == 1); // sample 27 should be here.

    for (Histogram::raw_type i = 0; i < Histogram::N_BUCKETS; i++) {
      printf("bucket %lu = %lu\n", i, h[i]);
    }
  }
}
